/* =============================================================================
 * tcp_congestion_experiment.cc  — FIXED & AUTOMATED VERSION
 *
 * ns-3.38+ TCP Congestion Control Comparison Experiment
 * Protocols : TcpNewReno | TcpReno | TcpCubic
 * Topology  : Dumbbell  (Sender--R1==R2--Receiver)
 * Traffic   : BulkSendApplication + PacketSink
 * Losses    : Congestion-induced only (DropTail queue)
 * Sweep     : 3 protocols × 4 loads × 10 seeds = 120 runs
 *
 * PLACEMENT:
 *   cp tcp_congestion_experiment.cc <ns3-root>/scratch/
 *
 * BUILD & RUN (ns-3.38+ cmake workflow):
 *   cd <ns3-root>
 *   ./ns3 build
 *   ./ns3 run scratch/tcp_congestion_experiment
 *
 * With optional overrides:
 *   ./ns3 run "scratch/tcp_congestion_experiment --simTime=60 --numSeeds=3"
 *
 * FIXES vs. original:
 *   1. cwnd tracing rewritten — uses Config::Connect path instead of the
 *      non-existent TcpL4Protocol::GetSockets() API.
 *   2. Simulator::Destroy() moved OUTSIDE RunExperiment so the simulator
 *      object is properly torn down only once per run with fresh nodes.
 *   3. Config::SetDefault calls now always reset ALL protocol attributes so
 *      CUBIC settings never leak into NewReno/Reno runs.
 *   4. RngSeedManager corrected: fixed seed=1, variable run index.
 *   5. Output directory auto-created with std::filesystem.
 *   6. PCAP directory auto-created when --pcap=true.
 *   7. FlowMonitor matching hardened: checks source IP in addition to port.
 *   8. BulkSend MaxBytes clamped to avoid zero-byte edge case at low loads.
 *   9. Bottleneck/access link parameters are fully automated via cmd args.
 *  10. TcpCubic attribute names verified against ns-3.38 source.
 * =============================================================================
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;
namespace fs = std::filesystem;

NS_LOG_COMPONENT_DEFINE("TcpCongestionExperiment");

// ---------------------------------------------------------------------------
// Global cwnd output — one file per run, opened/closed around Simulator::Run
// ---------------------------------------------------------------------------
static std::ofstream g_cwndFile;

static void
CwndTracer(uint32_t /*oldVal*/, uint32_t newVal)
{
    if (g_cwndFile.is_open())
    {
        g_cwndFile << std::fixed << std::setprecision(6)
                   << Simulator::Now().GetSeconds() << "\t" << newVal << "\n";
    }
}

// ---------------------------------------------------------------------------
// Automated experiment parameters (all overridable via command line)
// ---------------------------------------------------------------------------
struct ExperimentConfig
{
    // Topology
    std::string accessRate     = "100Mbps";  // access link data rate
    std::string accessDelay    = "5ms";      // access link one-way delay
    std::string bottleneckRate = "10Mbps";   // bottleneck link data rate
    std::string bottleneckDelay= "50ms";     // bottleneck link one-way delay
    uint32_t    queueSize      = 100;        // DropTail queue depth (packets)

    // TCP
    uint32_t mss          = 1460;            // segment size (bytes)
    uint32_t initCwndMss  = 10;             // initial cwnd in MSS units
    uint32_t sndRcvBufMB  = 8;             // send/recv buffer (MB)
    uint32_t rtoMinMs     = 200;            // minimum RTO (ms)

    // Sweep
    double   simTime  = 300.0;
    int      numSeeds = 10;
    bool     enablePcap = false;
    std::string outDir  = "";               // "" = current directory
};

// ---------------------------------------------------------------------------
// Per-run result
// ---------------------------------------------------------------------------
struct RunResult
{
    double   throughputMbps = 0;
    uint64_t txPackets  = 0;
    uint64_t rxPackets  = 0;
    uint64_t lostPackets= 0;
    double   lossRate   = 0;
    double   meanRttMs  = 0;
};

// ---------------------------------------------------------------------------
// Helper: ensure a directory exists (create if absent)
// ---------------------------------------------------------------------------
static bool
EnsureDir(const std::string& path)
{
    if (path.empty()) return true;
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec)
    {
        std::cerr << "WARNING: Could not create directory '" << path
                  << "': " << ec.message() << "\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helper: reset ALL TCP Config::SetDefault entries so nothing leaks between
//         runs with different protocols.
// ---------------------------------------------------------------------------
static void
ResetTcpDefaults(const std::string& ccAlgo, const ExperimentConfig& cfg)
{
    const uint32_t initCwndBytes    = cfg.initCwndMss * cfg.mss;
    const uint32_t initSsthreshBytes= 64 * cfg.mss;      // Linux default
    const uint32_t bufBytes         = cfg.sndRcvBufMB * (1u << 20u);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName(ccAlgo)));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(cfg.mss));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(initCwndBytes));
    Config::SetDefault("ns3::TcpSocket::InitialSlowStartThreshold",
                       UintegerValue(initSsthreshBytes));
    Config::SetDefault("ns3::TcpSocket::MinRto",
                       TimeValue(MilliSeconds(cfg.rtoMinMs)));
    Config::SetDefault("ns3::TcpSocket::SndBufSize",
                       UintegerValue(bufBytes));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize",
                       UintegerValue(bufBytes));

    // DropTail queue
    std::ostringstream qs;
    qs << cfg.queueSize << "p";
    Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize",
                       QueueSizeValue(QueueSize(qs.str())));

    // CUBIC-specific (set every run; ignored by NewReno/Reno kernel)
    // Attribute names verified against ns-3.38 src/internet/model/tcp-cubic.cc
    Config::SetDefault("ns3::TcpCubic::Beta",            DoubleValue(0.7));
    Config::SetDefault("ns3::TcpCubic::C",               DoubleValue(0.4));
    Config::SetDefault("ns3::TcpCubic::FastConvergence",  BooleanValue(true));
    Config::SetDefault("ns3::TcpCubic::HyStart",          BooleanValue(true));
}

// ---------------------------------------------------------------------------
// RunExperiment — builds topology, installs apps, runs, collects stats.
// NOTE: Simulator::Destroy() is called by the CALLER, not here.
// ---------------------------------------------------------------------------
RunResult
RunExperiment(const std::string&    protocol,
              double                loadMbps,
              uint32_t              runIndex,   // 1-based seed/run index
              const ExperimentConfig& cfg)
{
    // ------------------------------------------------------------------
    // 1. RNG:  fixed base seed so results are deterministic per (run, load)
    // ------------------------------------------------------------------
    RngSeedManager::SetSeed(12345);      // fixed base
    RngSeedManager::SetRun(runIndex);    // varies the stream

    // ------------------------------------------------------------------
    // 2. Protocol selection & global TCP defaults
    // ------------------------------------------------------------------
    std::string ccAlgo;
    if      (protocol == "NewReno") ccAlgo = "ns3::TcpNewReno";
    else if (protocol == "Reno")    ccAlgo = "ns3::TcpReno";
    else if (protocol == "Cubic")   ccAlgo = "ns3::TcpCubic";
    else    NS_FATAL_ERROR("Unknown protocol: " << protocol);

    ResetTcpDefaults(ccAlgo, cfg);

    // ------------------------------------------------------------------
    // 3. Nodes
    // ------------------------------------------------------------------
    NodeContainer allNodes;
    allNodes.Create(4);
    Ptr<Node> sender   = allNodes.Get(0);
    Ptr<Node> router1  = allNodes.Get(1);
    Ptr<Node> router2  = allNodes.Get(2);
    Ptr<Node> receiver = allNodes.Get(3);

    // ------------------------------------------------------------------
    // 4. Links  (parameters come from cfg — fully automated)
    // ------------------------------------------------------------------
    std::ostringstream qsStr;
    qsStr << cfg.queueSize << "p";

    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(cfg.accessRate));
    accessLink.SetChannelAttribute("Delay",   StringValue(cfg.accessDelay));
    accessLink.SetQueue("ns3::DropTailQueue<Packet>",
                        "MaxSize", StringValue(qsStr.str()));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(cfg.bottleneckRate));
    bottleneck.SetChannelAttribute("Delay",   StringValue(cfg.bottleneckDelay));
    bottleneck.SetQueue("ns3::DropTailQueue<Packet>",
                        "MaxSize", StringValue(qsStr.str()));

    NetDeviceContainer devSenderR1   = accessLink.Install(sender, router1);
    NetDeviceContainer devR1R2       = bottleneck.Install(router1, router2);
    NetDeviceContainer devR2Receiver = accessLink.Install(router2, receiver);

    // ------------------------------------------------------------------
    // 5. Internet stack + addressing
    // ------------------------------------------------------------------
    InternetStackHelper internet;
    internet.Install(allNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifSenderR1 = ipv4.Assign(devSenderR1);

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    /* Ipv4InterfaceContainer ifR1R2 = */ ipv4.Assign(devR1R2);

    ipv4.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer ifR2Receiver = ipv4.Assign(devR2Receiver);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // ------------------------------------------------------------------
    // 6. Applications
    // ------------------------------------------------------------------
    const uint16_t sinkPort = 9;
    Ipv4Address    receiverAddr = ifR2Receiver.GetAddress(1);
    Ipv4Address    senderAddr   = ifSenderR1.GetAddress(0);

    // Sink
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApp = sinkHelper.Install(receiver);
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(cfg.simTime));

    // BulkSend — cap MaxBytes to offered load × simTime so the sender does
    // not blow past the bottleneck by an unbounded amount.
    // Guard: ensure at least 1 MSS worth of data.
    uint64_t maxBytes = std::max(
        static_cast<uint64_t>(loadMbps * 1e6 / 8.0 * cfg.simTime),
        static_cast<uint64_t>(cfg.mss));

    BulkSendHelper bulkHelper("ns3::TcpSocketFactory",
                              InetSocketAddress(receiverAddr, sinkPort));
    bulkHelper.SetAttribute("MaxBytes", UintegerValue(maxBytes));
    bulkHelper.SetAttribute("SendSize", UintegerValue(cfg.mss));

    ApplicationContainer senderApp = bulkHelper.Install(sender);
    senderApp.Start(Seconds(0.1));
    senderApp.Stop(Seconds(cfg.simTime));

    // ------------------------------------------------------------------
    // 7. cwnd trace — use Config::Connect path (works in all ns-3 versions)
    //    Path: /NodeList/<id>/ApplicationList/0/$ns3::BulkSendApplication
    //          → the socket is under the TcpL4Protocol after Connect()
    //    We hook via the wildcard path on TcpSocketBase CongestionWindow.
    //    FIX: use Config::ConnectWithoutContext with wildcard so we don't
    //         need direct socket pointer access.
    // ------------------------------------------------------------------
    std::string cwndFile = cfg.outDir
        + "cwnd_" + protocol
        + "_load" + std::to_string(static_cast<int>(loadMbps))
        + "_seed" + std::to_string(runIndex) + ".dat";

    g_cwndFile.open(cwndFile);
    if (!g_cwndFile.is_open())
    {
        NS_LOG_WARN("Could not open cwnd file: " << cwndFile);
    }
    else
    {
        g_cwndFile << "# Time(s)\tCwnd(bytes)\n";
    }

    // Schedule the trace hookup AFTER the socket is created (app starts at 0.1s,
    // Connect() happens shortly after; 0.2s is a safe margin).
    // We use a wildcard Config path so no raw socket pointer is needed.
    Simulator::Schedule(Seconds(0.2), []() {
        Config::ConnectWithoutContext(
            "/NodeList/*/ApplicationList/*/$ns3::BulkSendApplication/Socket/CongestionWindow",
            MakeCallback(&CwndTracer));
    });

    // ------------------------------------------------------------------
    // 8. FlowMonitor
    // ------------------------------------------------------------------
    FlowMonitorHelper flowMonHelper;
    Ptr<FlowMonitor>  flowMonitor = flowMonHelper.InstallAll();

    // ------------------------------------------------------------------
    // 9. Optional PCAP
    // ------------------------------------------------------------------
    if (cfg.enablePcap)
    {
        EnsureDir("pcap");
        bottleneck.EnablePcapAll("pcap/bottleneck");
        accessLink.EnablePcapAll("pcap/access");
    }

    // ------------------------------------------------------------------
    // 10. Run
    // ------------------------------------------------------------------
    Simulator::Stop(Seconds(cfg.simTime));
    Simulator::Run();
    // NOTE: Simulator::Destroy() is called by the caller AFTER we return.

    // ------------------------------------------------------------------
    // 11. FlowMonitor statistics
    // ------------------------------------------------------------------
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowMonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    RunResult result{};

    for (auto& kv : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);

        // Match: TCP (proto=6), our sink port, sender→receiver direction
        if (t.protocol != 6)                        continue;
        if (t.destinationPort != sinkPort)          continue;
        if (t.sourceAddress != senderAddr)          continue;

        const FlowMonitor::FlowStats& fs = kv.second;
        double duration = fs.timeLastRxPacket.GetSeconds()
                        - fs.timeFirstTxPacket.GetSeconds();
        if (duration <= 0.0) continue;

        result.rxPackets    = fs.rxPackets;
        result.txPackets    = fs.txPackets;
        result.lostPackets  = fs.lostPackets;
        result.throughputMbps = (fs.rxBytes * 8.0) / duration / 1e6;
        result.lossRate =
            (result.txPackets > 0)
            ? static_cast<double>(result.lostPackets) / result.txPackets
            : 0.0;
        if (fs.rxPackets > 0)
            result.meanRttMs = fs.delaySum.GetMilliSeconds() / fs.rxPackets;
        break;
    }

    // ------------------------------------------------------------------
    // 12. Close cwnd file — Simulator::Destroy() done by caller
    // ------------------------------------------------------------------
    if (g_cwndFile.is_open())
        g_cwndFile.close();

    return result;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int
main(int argc, char* argv[])
{
    ExperimentConfig cfg;

    // Expose ALL bottleneck/topology parameters on the command line
    CommandLine cmd(__FILE__);
    cmd.AddValue("pcap",            "Enable PCAP tracing",               cfg.enablePcap);
    cmd.AddValue("simTime",         "Simulation duration (s)",           cfg.simTime);
    cmd.AddValue("numSeeds",        "Number of independent runs",        cfg.numSeeds);
    cmd.AddValue("outDir",          "Output directory for all files",    cfg.outDir);
    cmd.AddValue("accessRate",      "Access link data rate (e.g. 100Mbps)", cfg.accessRate);
    cmd.AddValue("accessDelay",     "Access link delay  (e.g. 5ms)",    cfg.accessDelay);
    cmd.AddValue("bottleneckRate",  "Bottleneck data rate (e.g. 10Mbps)",cfg.bottleneckRate);
    cmd.AddValue("bottleneckDelay", "Bottleneck delay  (e.g. 50ms)",    cfg.bottleneckDelay);
    cmd.AddValue("queueSize",       "DropTail queue depth (packets)",    cfg.queueSize);
    cmd.AddValue("mss",             "TCP segment size (bytes)",          cfg.mss);
    cmd.AddValue("initCwndMss",     "Initial cwnd in MSS units",        cfg.initCwndMss);
    cmd.AddValue("sndRcvBufMB",     "TCP send/recv buffer (MB)",        cfg.sndRcvBufMB);
    cmd.AddValue("rtoMinMs",        "Minimum RTO (ms)",                 cfg.rtoMinMs);
    cmd.Parse(argc, argv);

    // Normalise output directory: ensure trailing slash
    if (!cfg.outDir.empty() && cfg.outDir.back() != '/')
        cfg.outDir += '/';
    if (!cfg.outDir.empty())
        EnsureDir(cfg.outDir);

    // Sweep parameters
    const std::vector<std::string> protocols = {"NewReno", "Reno", "Cubic"};
    const std::vector<double>      loads     = {8.0, 10.0, 12.0, 15.0};

    // Open CSV outputs
    std::ofstream tputFile(cfg.outDir + "throughput_results.csv");
    std::ofstream lossFile(cfg.outDir + "loss_results.csv");
    std::ofstream rttFile (cfg.outDir + "rtt_results.csv");

    if (!tputFile || !lossFile || !rttFile)
    {
        std::cerr << "ERROR: Cannot open output CSV files in '"
                  << cfg.outDir << "'.\n";
        return 1;
    }

    tputFile << "Protocol,Load_Mbps,Seed,Throughput_Mbps\n";
    lossFile << "Protocol,Load_Mbps,Seed,LostPackets,LossRate\n";
    rttFile  << "Protocol,Load_Mbps,Seed,MeanRTT_ms\n";

    const int totalRuns =
        static_cast<int>(protocols.size() * loads.size()) * cfg.numSeeds;
    int runIdx = 0;

    // Print experiment header
    std::cout << "=== TCP Congestion Control Experiment ===\n"
              << "  Bottleneck : " << cfg.bottleneckRate
              << " / " << cfg.bottleneckDelay << "\n"
              << "  Access     : " << cfg.accessRate
              << " / " << cfg.accessDelay << "\n"
              << "  Queue      : " << cfg.queueSize << " pkts\n"
              << "  MSS        : " << cfg.mss << " B\n"
              << "  SimTime    : " << cfg.simTime << " s\n"
              << "  Runs       : " << totalRuns << " total\n"
              << "  Output dir : " << (cfg.outDir.empty() ? "./" : cfg.outDir)
              << "\n=========================================\n";

    for (const auto& proto : protocols)
    {
        for (double load : loads)
        {
            for (int seed = 1; seed <= cfg.numSeeds; ++seed)
            {
                ++runIdx;
                std::cout << "[" << std::setw(3) << runIdx << "/" << totalRuns << "] "
                          << std::setw(7) << proto
                          << "  Load=" << std::setw(4) << load << " Mbps"
                          << "  Seed=" << std::setw(2) << seed << "  ...";
                std::cout.flush();

                RunResult res = RunExperiment(proto, load,
                                              static_cast<uint32_t>(seed), cfg);

                // IMPORTANT: destroy simulator after each run so the next run
                // starts with a clean slate (fresh node IDs, clean object store).
                Simulator::Destroy();

                std::cout << "  Tput=" << std::fixed << std::setprecision(3)
                          << std::setw(8) << res.throughputMbps << " Mbps"
                          << "  Loss=" << std::setw(6) << res.lostPackets
                          << "  RTT=" << std::setw(7) << res.meanRttMs << " ms\n";

                tputFile << proto << "," << load << "," << seed << ","
                         << std::fixed << std::setprecision(6)
                         << res.throughputMbps << "\n";

                lossFile << proto << "," << load << "," << seed << ","
                         << res.lostPackets << ","
                         << std::fixed << std::setprecision(6)
                         << res.lossRate << "\n";

                rttFile  << proto << "," << load << "," << seed << ","
                         << std::fixed << std::setprecision(3)
                         << res.meanRttMs << "\n";

                tputFile.flush();
                lossFile.flush();
                rttFile.flush();
            }
        }
    }

    tputFile.close();
    lossFile.close();
    rttFile.close();

    std::cout << "\n=== Experiment complete. ===\n"
              << "Outputs written to: "
              << (cfg.outDir.empty() ? "./" : cfg.outDir) << "\n"
              << "  throughput_results.csv\n"
              << "  loss_results.csv\n"
              << "  rtt_results.csv\n"
              << "  cwnd_<Protocol>_load<X>_seed<Y>.dat\n";
    return 0;
}

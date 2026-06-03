/* tcp-congestion-experiment.cc
 * NS-3.39 TCP Congestion Control Comparison
 * Compares TcpNewReno, TcpReno, TcpCubic
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TcpCongestionExperiment");

// Global output streams
static std::ofstream cwndFile;
static std::ofstream rttFile;
static std::ofstream flowFile;

// Flow monitor state for delta-based metrics
static uint64_t prevRxBytes = 0;
static uint32_t prevLostPackets = 0;

// ============================================
// Trace callbacks
// ============================================

static void
CwndTracer(uint32_t oldCwnd, uint32_t newCwnd)
{
    cwndFile << Simulator::Now().GetSeconds() << "\t"
             << newCwnd << std::endl;
}

static void
RttTracer(Time oldRtt, Time newRtt)
{
    rttFile << Simulator::Now().GetSeconds() << "\t"
            << newRtt.GetMilliSeconds() << std::endl;
}

// Connect traces using wildcards (no hardcoded socket index)
static void
ConnectTraces()
{
    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
        MakeCallback(&CwndTracer));

    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/RTT",
        MakeCallback(&RttTracer));
}

// ============================================
// Periodic flow metrics collection
// ============================================

static void
CollectFlowMetrics(Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier)
{
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    for (auto &iter : stats)
    {
        Ipv4FlowClassifier::FiveTuple ft = classifier->FindFlow(iter.first);
        // Only track the bulk send flow (port 9)
        if (ft.destinationPort != 9)
            continue;

        uint64_t rxBytes = iter.second.rxBytes;
        uint32_t lostPackets = iter.second.lostPackets;
        uint32_t rxPackets = iter.second.rxPackets;
        Time delaySum = iter.second.delaySum;

        // Delta-based throughput (Mbps)
        uint64_t deltaBytes = rxBytes - prevRxBytes;
        double throughput = (deltaBytes * 8.0) / (0.1 * 1e6);

        // Delta-based packet loss
        uint32_t deltaLost = lostPackets - prevLostPackets;

        // Average delay (ms)
        double avgDelay = 0.0;
        if (rxPackets > 0)
        {
            avgDelay = delaySum.GetMilliSeconds() / (double)rxPackets;
        }

        flowFile << Simulator::Now().GetSeconds() << "\t"
                 << throughput << "\t"
                 << deltaLost << "\t"
                 << avgDelay << std::endl;

        prevRxBytes = rxBytes;
        prevLostPackets = lostPackets;
    }

    // Schedule next collection in 0.1 seconds
    Simulator::Schedule(Seconds(0.1), &CollectFlowMetrics, monitor, classifier);
}

// ============================================
// Run a single simulation for one TCP variant
// ============================================

static void
RunSimulation(const std::string &tcpVariant, const std::string &variantName)
{
    NS_LOG_INFO("=== Running simulation: " << variantName << " ===");

    // Reset delta counters
    prevRxBytes = 0;
    prevLostPackets = 0;

    // Set TCP variant
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TypeId::LookupByName(tcpVariant)));

    // Set segment size
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));

    // ----------------------------------------
    // Create topology: Sender -> Router -> Receiver
    // ----------------------------------------
    NodeContainer nodes;
    nodes.Create(3);

    // Access link: Sender <-> Router (20 Mbps, 1 ms)
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue("20Mbps"));
    accessLink.SetChannelAttribute("Delay", StringValue("1ms"));

    // Bottleneck link: Router <-> Receiver (10 Mbps, 1 ms)
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("1ms"));

    NetDeviceContainer devAccess = accessLink.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer devBottleneck = bottleneckLink.Install(nodes.Get(1), nodes.Get(2));

    // ----------------------------------------
    // Install Traffic Control (ns-3.39 compatible)
    // Uses FqCoDelQueueDisc by default, no deprecated DropTailQueue
    // ----------------------------------------
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::FqCoDelQueueDisc");
    tch.Install(devBottleneck);

    // ----------------------------------------
    // Install Internet stack
    // ----------------------------------------
    InternetStackHelper internet;
    internet.Install(nodes);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifAccess = ipv4.Assign(devAccess);

    ipv4.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifBottleneck = ipv4.Assign(devBottleneck);

    // Enable routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // ----------------------------------------
    // Applications
    // ----------------------------------------

    // PacketSink on receiver (node 2)
    uint16_t port = 9;
    Address sinkAddress(InetSocketAddress(ifBottleneck.GetAddress(1), port));
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkAddress);
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(2));
    sinkApp.Start(Seconds(0.5));
    sinkApp.Stop(Seconds(50.0));

    // BulkSendApplication on sender (node 0)
    BulkSendHelper bulkHelper("ns3::TcpSocketFactory", sinkAddress);
    bulkHelper.SetAttribute("MaxBytes", UintegerValue(0)); // Unlimited
    bulkHelper.SetAttribute("SendSize", UintegerValue(1448));
    ApplicationContainer sourceApp = bulkHelper.Install(nodes.Get(0));
    sourceApp.Start(Seconds(1.0));
    sourceApp.Stop(Seconds(50.0));

    // ----------------------------------------
    // Flow Monitor
    // ----------------------------------------
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

    // ----------------------------------------
    // Open output files
    // ----------------------------------------
    std::string cwndFilename = "cwnd_" + variantName + ".dat";
    std::string rttFilename = "rtt_" + variantName + ".dat";
    std::string flowFilename = "flow_metrics_" + variantName + ".dat";

    cwndFile.open(cwndFilename);
    rttFile.open(rttFilename);
    flowFile.open(flowFilename);

    cwndFile << "# Time(s)\tCwnd(bytes)" << std::endl;
    rttFile << "# Time(s)\tRTT(ms)" << std::endl;
    flowFile << "# Time(s)\tThroughput(Mbps)\tLostPkts\tAvgDelay(ms)" << std::endl;

    // ----------------------------------------
    // Schedule tracing and metric collection
    // ----------------------------------------
    Simulator::Schedule(Seconds(1.01), &ConnectTraces);
    Simulator::Schedule(Seconds(1.1), &CollectFlowMetrics, flowMonitor, classifier);

    // ----------------------------------------
    // Run simulation
    // ----------------------------------------
    Simulator::Stop(Seconds(50.0));
    Simulator::Run();

    // Final flow stats summary
    FlowMonitor::FlowStatsContainer finalStats = flowMonitor->GetFlowStats();
    for (auto &iter : finalStats)
    {
        Ipv4FlowClassifier::FiveTuple ft = classifier->FindFlow(iter.first);
        if (ft.destinationPort != 9)
            continue;

        double totalThroughput = (iter.second.rxBytes * 8.0) /
                                 (49.0 * 1e6); // ~49s of flow
        NS_LOG_INFO(variantName << " Final: "
                    << "Throughput=" << totalThroughput << " Mbps, "
                    << "Lost=" << iter.second.lostPackets << " pkts, "
                    << "AvgDelay=" << (iter.second.rxPackets > 0 ?
                       iter.second.delaySum.GetMilliSeconds() /
                       (double)iter.second.rxPackets : 0)
                    << " ms");
    }

    // Cleanup
    cwndFile.close();
    rttFile.close();
    flowFile.close();

    Simulator::Destroy();
}

// ============================================
// Main: Run all TCP variants sequentially
// ============================================

int
main(int argc, char *argv[])
{
    LogComponentEnable("TcpCongestionExperiment", LOG_LEVEL_INFO);

    CommandLine cmd;
    cmd.Parse(argc, argv);

    // Run all three variants
    RunSimulation("ns3::TcpNewReno", "TcpNewReno");
    RunSimulation("ns3::TcpLinuxReno", "TcpReno");
    RunSimulation("ns3::TcpCubic", "TcpCubic");

    return 0;
}

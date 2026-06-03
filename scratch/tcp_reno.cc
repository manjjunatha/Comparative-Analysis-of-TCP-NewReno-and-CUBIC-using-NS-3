#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include <fstream>
#include <map>

using namespace ns3;

std::ofstream cwndFile;
std::ofstream rttFile;

// CWND trace
static void CwndChange(uint32_t oldCwnd, uint32_t newCwnd)
{
    cwndFile << Simulator::Now().GetSeconds() << "\t"
             << newCwnd / 1024.0 << std::endl;
}

// RTT trace
static void RttChange(Time oldRtt, Time newRtt)
{
    rttFile << Simulator::Now().GetSeconds() << "\t"
            << newRtt.GetMilliSeconds() << std::endl;
}

int main(int argc, char* argv[])
{
    Time::SetResolution(Time::NS);

    // TCP NewReno (simple + stable)
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));

    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));

    // ===== TOPOLOGY =====
    // 2 senders → 1 router → 1 receiver
    NodeContainer nodes;
    nodes.Create(4);

    NodeContainer n0r1 = NodeContainer(nodes.Get(0), nodes.Get(2));
    NodeContainer n1r1 = NodeContainer(nodes.Get(1), nodes.Get(2));
    NodeContainer r1n2 = NodeContainer(nodes.Get(2), nodes.Get(3));

    // Access links (fast)
    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("20Mbps"));
    access.SetChannelAttribute("Delay", StringValue("1ms"));

    // Bottleneck link (FORCES CONGESTION)
    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue("2Mbps"));
    bottleneck.SetChannelAttribute("Delay", StringValue("50ms"));
    bottleneck.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("30p"));

    NetDeviceContainer d0r1 = access.Install(n0r1);
    NetDeviceContainer d1r1 = access.Install(n1r1);
    NetDeviceContainer d1n2 = bottleneck.Install(r1n2);

    // Internet stack
    InternetStackHelper stack;
    stack.Install(nodes);

    // IP addressing
    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.0");
    auto i0r1 = address.Assign(d0r1);

    address.SetBase("10.1.2.0", "255.255.255.0");
    auto i1r1 = address.Assign(d1r1);

    address.SetBase("10.1.3.0", "255.255.255.0");
    auto i1n2 = address.Assign(d1n2);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t port = 8080;

    // ===== RECEIVER =====
    PacketSinkHelper sink("ns3::TcpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), port));

    auto sinkApp = sink.Install(nodes.Get(3));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(50.0));

    // ===== SENDER 1 =====
    BulkSendHelper source1("ns3::TcpSocketFactory",
        InetSocketAddress(i1n2.GetAddress(1), port));
    source1.SetAttribute("MaxBytes", UintegerValue(0));

    auto app1 = source1.Install(nodes.Get(0));
    app1.Start(Seconds(1.0));
    app1.Stop(Seconds(50.0));

    // ===== SENDER 2 =====
    BulkSendHelper source2("ns3::TcpSocketFactory",
        InetSocketAddress(i1n2.GetAddress(1), port));
    source2.SetAttribute("MaxBytes", UintegerValue(0));

    auto app2 = source2.Install(nodes.Get(1));
    app2.Start(Seconds(1.0));
    app2.Stop(Seconds(50.0));

    // ===== OUTPUT FILES =====
    cwndFile.open("cwnd_final.dat");
    rttFile.open("rtt_final.dat");

// Attach traces (important timing)
Simulator::Schedule(Seconds(1.5), [](){
    Config::ConnectWithoutContext(
    "/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/CongestionWindow",
    MakeCallback(&CwndChange));

    Config::ConnectWithoutContext(
    "/NodeList/0/$ns3::TcpL4Protocol/SocketList/0/RTT",
    MakeCallback(&RttChange));
});

// FlowMonitor setup
FlowMonitorHelper flowmon;
Ptr<FlowMonitor> monitor = flowmon.InstallAll();

Simulator::Stop(Seconds(50.0));
Simulator::Run();

// Collect flow stats AFTER run
monitor->CheckForLostPackets();
auto stats = monitor->GetFlowStats();

// Open output file
std::ofstream flowFile("flow.dat");

// Loop (VERY IMPORTANT — keep brackets correct)
for (auto const& [id, stat] : stats)
{
    double duration = 49.0; // steady-state (50 - 1)

    double throughput = (stat.rxBytes * 8.0) / (duration * 1e6);

    flowFile << id << "\t"
             << throughput << "\t"
             << stat.lostPackets << std::endl;
}

// Close file
flowFile.close();

Simulator::Destroy();
return 0;
}

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
std::ofstream flowFile;

static void CwndTracer(uint32_t oldval, uint32_t newval)
{
    cwndFile << Simulator::Now().GetSeconds() << "\t" << newval << std::endl;
}

static void RttTracer(Time oldval, Time newval)
{
    rttFile << Simulator::Now().GetSeconds() << "\t" << newval.GetMilliSeconds() << std::endl;
}

void TraceTcp()
{
    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
        MakeCallback(&CwndTracer));

    Config::ConnectWithoutContext(
        "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/RTT",
        MakeCallback(&RttTracer));
}

void RunExperiment(std::string variant)
{
    std::cout << "Running: " << variant << std::endl;

    if (variant == "NewReno")
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           TypeIdValue(TcpNewReno::GetTypeId()));
    else if (variant == "Reno")
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           TypeIdValue(TcpReno::GetTypeId()));
    else if (variant == "Cubic")
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           TypeIdValue(TcpCubic::GetTypeId()));

    NodeContainer nodes;
    nodes.Create(3);

    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("20Mbps"));
    access.SetChannelAttribute("Delay", StringValue("1ms"));

    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    bottleneck.SetChannelAttribute("Delay", StringValue("1ms"));

    NetDeviceContainer d0d1 = access.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer d1d2 = bottleneck.Install(nodes.Get(1), nodes.Get(2));

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper addr;
    addr.SetBase("10.1.1.0", "255.255.255.0");
    auto i0i1 = addr.Assign(d0d1);

    addr.SetBase("10.1.2.0", "255.255.255.0");
    auto i1i2 = addr.Assign(d1d2);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    uint16_t port = 5000;

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sinkHelper.Install(nodes.Get(2));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(50.0));

    BulkSendHelper source("ns3::TcpSocketFactory",
                          InetSocketAddress(i1i2.GetAddress(1), port));
    source.SetAttribute("MaxBytes", UintegerValue(0));

    ApplicationContainer sourceApp = source.Install(nodes.Get(0));
    sourceApp.Start(Seconds(1.0));
    sourceApp.Stop(Seconds(50.0));

    cwndFile.open("cwnd_" + variant + ".dat");
    rttFile.open("rtt_" + variant + ".dat");
    flowFile.open("flow_metrics_" + variant + ".dat");

    Simulator::Schedule(Seconds(1.1), &TraceTcp);

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    std::map<FlowId, uint64_t> lastRx;
    std::map<FlowId, uint32_t> lastLost;

    for (double t = 1.0; t <= 50.0; t += 0.1)
    {
        Simulator::Schedule(Seconds(t), [&]() {
            monitor->CheckForLostPackets();
            auto stats = monitor->GetFlowStats();

            for (auto &flow : stats)
            {
                FlowId id = flow.first;
                auto &st = flow.second;

                double rxDelta = st.rxBytes - lastRx[id];
                double throughput = (rxDelta * 8.0) / (0.1 * 1e6);

                uint32_t lossDelta = st.lostPackets - lastLost[id];
                double avgDelay = (st.rxPackets > 0)
                                      ? st.delaySum.GetSeconds() / st.rxPackets
                                      : 0;

                flowFile << Simulator::Now().GetSeconds() << "\t"
                         << throughput << "\t"
                         << lossDelta << "\t"
                         << avgDelay << std::endl;

                lastRx[id] = st.rxBytes;
                lastLost[id] = st.lostPackets;
            }
        });
    }

    Simulator::Stop(Seconds(50.0));
    Simulator::Run();
    Simulator::Destroy();

    cwndFile.close();
    rttFile.close();
    flowFile.close();
}

int main()
{
    RunExperiment("NewReno");
    RunExperiment("Reno");
    RunExperiment("Cubic");
    return 0;
}

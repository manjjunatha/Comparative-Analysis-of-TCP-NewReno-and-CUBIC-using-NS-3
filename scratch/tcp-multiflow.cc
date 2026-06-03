#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

int main(int argc, char *argv[])
{
    std::string tcpType = "NewReno";
    std::string bottleneckBw = "10Mbps";
    std::string delay = "50ms";
    uint32_t queueSize = 10;

    CommandLine cmd;
    cmd.AddValue("tcpType", "NewReno or Cubic", tcpType);
    cmd.AddValue("bottleneckBw", "Bottleneck bandwidth", bottleneckBw);
    cmd.AddValue("delay", "Link delay", delay);
    cmd.AddValue("queueSize", "Queue size in packets", queueSize);
    cmd.Parse(argc, argv);

    // TCP Selection
    if (tcpType == "NewReno")
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           TypeIdValue(TcpNewReno::GetTypeId()));
    }
    else
    {
        Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                           TypeIdValue(TcpCubic::GetTypeId()));
    }

    // Nodes
    NodeContainer senders;
    senders.Create(5);

    NodeContainer router;
    router.Create(1);

    NodeContainer receiver;
    receiver.Create(1);

    InternetStackHelper stack;
    stack.InstallAll();

    // Access links
    PointToPointHelper access;
    access.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    access.SetChannelAttribute("Delay", StringValue("1ms"));

    // Bottleneck link
    PointToPointHelper bottleneck;
    bottleneck.SetDeviceAttribute("DataRate", StringValue(bottleneckBw));
    bottleneck.SetChannelAttribute("Delay", StringValue(delay));

    std::ostringstream queueStr;
    queueStr << queueSize << "p";
    bottleneck.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue(queueStr.str()));

    // Install links
    NetDeviceContainer d[5];
    for (int i = 0; i < 5; i++)
        d[i] = access.Install(senders.Get(i), router.Get(0));

    NetDeviceContainer dBottle = bottleneck.Install(router.Get(0), receiver.Get(0));

    // IP assignment
    Ipv4AddressHelper addr;
    Ipv4InterfaceContainer interfaces[5];

    for (int i = 0; i < 5; i++)
    {
        std::ostringstream subnet;
        subnet << "10.1." << i+1 << ".0";
        addr.SetBase(subnet.str().c_str(), "255.255.255.0");
        interfaces[i] = addr.Assign(d[i]);
    }

    addr.SetBase("10.1.10.0", "255.255.255.0");
    Ipv4InterfaceContainer iBottle = addr.Assign(dBottle);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Applications
    uint16_t port = 5000;

    PacketSinkHelper sink("ns3::TcpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), port));
    auto sinkApp = sink.Install(receiver.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(100.0));

    for (int i = 0; i < 5; i++)
    {
        BulkSendHelper src("ns3::TcpSocketFactory",
                           InetSocketAddress(iBottle.GetAddress(1), port));
        src.SetAttribute("MaxBytes", UintegerValue(0));

        auto app = src.Install(senders.Get(i));
        app.Start(Seconds(1.0 + i * 0.1));  // staggered start
        app.Stop(Seconds(100.0));
    }

    // FlowMonitor
    FlowMonitorHelper flowmon;
    auto monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(100.0));
    Simulator::Run();

    monitor->CheckForLostPackets();

    auto stats = monitor->GetFlowStats();

    double throughput[5] = {0};
    double totalThroughput = 0, totalDelay = 0;
    double rxPackets = 0, txPackets = 0;

    int idx = 0;

    for (auto &flow : stats)
    {
        if (flow.second.rxBytes > 0 && idx < 5)
        {
            double thr = flow.second.rxBytes * 8.0 / 100.0 / 1e6;
            throughput[idx++] = thr;
            totalThroughput += thr;

            totalDelay += flow.second.delaySum.GetSeconds();
            rxPackets += flow.second.rxPackets;
            txPackets += flow.second.txPackets;
        }
    }

    double lossRate = (txPackets - rxPackets) / txPackets;
    double avgDelay = totalDelay / rxPackets;

    double sum = 0, sq = 0;
    for (int i = 0; i < 5; i++)
    {
        sum += throughput[i];
        sq += throughput[i] * throughput[i];
    }

    double fairness = (sum * sum) / (5 * sq);

    // Save CSV
    std::ofstream out("final_results.csv", std::ios::app);

    out << tcpType << "," << bottleneckBw << "," << delay << "," << queueSize;

    for (int i = 0; i < 5; i++)
        out << "," << throughput[i];

    out << "," << totalThroughput << "," << lossRate << "," << avgDelay << "," << fairness << std::endl;

    out.close();

    Simulator::Destroy();
    return 0;
}

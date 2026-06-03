#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/tcp-cubic.h"

#include <fstream>
#include <map>

using namespace ns3;

std::ofstream cwndFile;
std::ofstream rttFile;
std::ofstream flowFile;

// =========================
// CWND + RTT tracing
// =========================
static void CwndChange(uint32_t oldCwnd, uint32_t newCwnd)
{
  cwndFile << Simulator::Now().GetSeconds() << "\t"
           << newCwnd / 1024.0 << std::endl;
}

static void RttChange(Time oldRtt, Time newRtt)
{
  rttFile << Simulator::Now().GetSeconds() << "\t"
          << newRtt.GetMilliSeconds() << std::endl;
}

// =========================
// Flow sampling (FIXED throughput)
// =========================
void SampleFlowStats(Ptr<FlowMonitor> monitor,
                     std::map<uint32_t, uint64_t>& prevRxBytes,
                     std::map<uint32_t, uint32_t>& prevLostPkts,
                     double& lastTime)
{
  double now = Simulator::Now().GetSeconds();
  double interval = now - lastTime;
  lastTime = now;

  auto stats = monitor->GetFlowStats();

  for (auto const& [flowId, stat] : stats)
  {
    uint64_t rxDelta = stat.rxBytes - prevRxBytes[flowId];
    prevRxBytes[flowId] = stat.rxBytes;

    uint32_t lostDelta = stat.lostPackets - prevLostPkts[flowId];
    prevLostPkts[flowId] = stat.lostPackets;

    double throughput = (interval > 0) ? (rxDelta * 8.0) / (interval * 1e6) : 0;

    flowFile << now << "\t" << flowId << "\t"
             << throughput << "\t"
             << lostDelta << "\t"
             << stat.delaySum.GetSeconds() << "\t"
             << 0.0 << std::endl;
  }

  Simulator::Schedule(Seconds(0.1),
                      &SampleFlowStats, monitor,
                      std::ref(prevRxBytes),
                      std::ref(prevLostPkts),
                      std::ref(lastTime));
}

// =========================
// MAIN
// =========================
int main(int argc, char* argv[])
{
  Time::SetResolution(Time::NS);

  // TCP = CUBIC
  Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                     TypeIdValue(TcpCubic::GetTypeId()));

  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
  Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(10));

  NodeContainer nodes;
  nodes.Create(3);

  NodeContainer n0r1 = NodeContainer(nodes.Get(0), nodes.Get(1));
  NodeContainer r1n2 = NodeContainer(nodes.Get(1), nodes.Get(2));

  // Access link
  PointToPointHelper access;
  access.SetDeviceAttribute("DataRate", StringValue("20Mbps"));
  access.SetChannelAttribute("Delay", StringValue("1ms"));

  // Bottleneck link (tweaked queue)
  PointToPointHelper bottleneck;
  bottleneck.SetDeviceAttribute("DataRate", StringValue("12Mbps"));
  bottleneck.SetChannelAttribute("Delay", StringValue("50ms"));
  bottleneck.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("40p")); // ✅ updated

  NetDeviceContainer d0r1 = access.Install(n0r1);
  NetDeviceContainer d1n2 = bottleneck.Install(r1n2);

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i0r1 = address.Assign(d0r1);

  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i1n2 = address.Assign(d1n2);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  uint16_t port = 8080;

  // Sink
  PacketSinkHelper sink("ns3::TcpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(nodes.Get(2));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(50.0));

  // Source
  BulkSendHelper source("ns3::TcpSocketFactory",
                        InetSocketAddress(i1n2.GetAddress(1), port));
  source.SetAttribute("MaxBytes", UintegerValue(0));

  ApplicationContainer sourceApp = source.Install(nodes.Get(0));
  sourceApp.Start(Seconds(1.0));
  sourceApp.Stop(Seconds(50.0));

  // Second flow (competition)
  uint16_t port2 = 8081;

  PacketSinkHelper sink2("ns3::TcpSocketFactory",
      InetSocketAddress(Ipv4Address::GetAny(), port2));
  ApplicationContainer sinkApp2 = sink2.Install(nodes.Get(2));
  sinkApp2.Start(Seconds(0.0));
  sinkApp2.Stop(Seconds(50.0));

  BulkSendHelper source2("ns3::TcpSocketFactory",
      InetSocketAddress(i1n2.GetAddress(1), port2));
  source2.SetAttribute("MaxBytes", UintegerValue(0));

  ApplicationContainer sourceApp2 = source2.Install(nodes.Get(0));
  sourceApp2.Start(Seconds(1.05));
  sourceApp2.Stop(Seconds(50.0));

  // Output files
  cwndFile.open("cwnd_cubic_updated.dat");
  rttFile.open("rtt_cubic_updated.dat");
  flowFile.open("flow_metrics_per_sec_cubic_updated.dat");

  // ✅ FIX: wildcard tracing
  Simulator::Schedule(Seconds(1.1), [](){
    Config::ConnectWithoutContext(
      "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
      MakeCallback(&CwndChange));

    Config::ConnectWithoutContext(
      "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/RTT",
      MakeCallback(&RttChange));
  });

  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  std::map<uint32_t, uint64_t> prevRxBytes;
  std::map<uint32_t, uint32_t> prevLostPkts;
  double lastTime = 1.1;

  Simulator::Schedule(Seconds(1.1),
                      &SampleFlowStats, monitor,
                      std::ref(prevRxBytes),
                      std::ref(prevLostPkts),
                      std::ref(lastTime));

  Simulator::Stop(Seconds(50.0));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

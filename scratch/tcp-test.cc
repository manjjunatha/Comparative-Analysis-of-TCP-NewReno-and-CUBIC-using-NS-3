#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include "ns3/error-model.h"

using namespace ns3;

static void
CwndChange (uint32_t oldCwnd, uint32_t newCwnd)
{
  static std::ofstream outFile("cwnd.txt");
  outFile << Simulator::Now().GetSeconds()
          << "\t" << newCwnd << std::endl;
}

int main (int argc, char *argv[])
{
  std::string tcpType = "NewReno";
  std::string bandwidth = "10Mbps";
  std::string delay = "10ms";
  double lossRate = 0.0;
  double simTime = 100.0;

  CommandLine cmd;
  cmd.AddValue("tcpType", "NewReno or Cubic", tcpType);
  cmd.AddValue("bandwidth", "Link bandwidth", bandwidth);
  cmd.AddValue("delay", "Link delay", delay);
  cmd.AddValue("lossRate", "Packet loss rate", lossRate);
  cmd.AddValue("simTime", "Simulation time", simTime);
  cmd.Parse(argc, argv);

  if (tcpType == "NewReno")
  {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));
  }
  else if (tcpType == "Cubic")
  {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpCubic::GetTypeId()));
  }

  NodeContainer nodes;
  nodes.Create(2);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute("DataRate", StringValue(bandwidth));
  pointToPoint.SetChannelAttribute("Delay", StringValue(delay));

  NetDeviceContainer devices = pointToPoint.Install(nodes);

  if (lossRate > 0.0)
  {
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
    em->SetAttribute("ErrorRate", DoubleValue(lossRate));
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
  }

  InternetStackHelper stack;
  stack.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);

  uint16_t port = 8080;

  BulkSendHelper source("ns3::TcpSocketFactory",
                        InetSocketAddress(interfaces.GetAddress(1), port));
  source.SetAttribute("MaxBytes", UintegerValue(0));

  ApplicationContainer sourceApps = source.Install(nodes.Get(0));
  sourceApps.Start(Seconds(1.0));
  sourceApps.Stop(Seconds(simTime));

  PacketSinkHelper sink("ns3::TcpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));

  ApplicationContainer sinkApps = sink.Install(nodes.Get(1));
  sinkApps.Start(Seconds(0.0));
  sinkApps.Stop(Seconds(simTime));

  // Enable PCAP for Wireshark
  pointToPoint.EnablePcapAll("tcp-test");

  // Enable NetAnim
  AnimationInterface anim("anim.xml");

  // Attach cwnd trace AFTER socket is created
  Simulator::Schedule(Seconds(1.1), []() {
    Config::ConnectWithoutContext(
      "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
      MakeCallback(&CwndChange));
  });

  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  monitor->CheckForLostPackets();

  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  for (auto const &flow : stats)
  {
    double duration =
      flow.second.timeLastRxPacket.GetSeconds() -
      flow.second.timeFirstTxPacket.GetSeconds();

    double throughput =
      flow.second.rxBytes * 8.0 / duration / 1000000;

    std::cout << "\n===== RESULTS =====\n";
    std::cout << "Throughput: " << throughput << " Mbps\n";

    double loss =
      (flow.second.txPackets - flow.second.rxPackets) * 100.0 /
      flow.second.txPackets;

    std::cout << "Loss %: " << loss << "\n";

    double avgDelay =
      flow.second.delaySum.GetSeconds() /
      flow.second.rxPackets;

    std::cout << "Average Delay: "
              << avgDelay * 1000 << " ms\n";
  }

  monitor->SerializeToXmlFile("flowmon.xml", true, true);

  Simulator::Destroy();
  return 0;
}

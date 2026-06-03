#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"

using namespace ns3;

int main ()
{
  double simTime = 10.0;

  // 🔹 Select TCP Reno (NewReno)
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType",
                      TypeIdValue (TcpNewReno::GetTypeId()));

  // Create nodes
  NodeContainer nodes;
  nodes.Create (2);

  // Create link
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer devices = p2p.Install (nodes);

  // Enable Wireshark capture
  p2p.EnablePcapAll("reno");

  // Install TCP/IP
  InternetStackHelper stack;
  stack.Install (nodes);

  // Assign IP
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  // Receiver
  uint16_t port = 8080;
  PacketSinkHelper sink ("ns3::TcpSocketFactory",
                         InetSocketAddress (Ipv4Address::GetAny (), port));

  ApplicationContainer sinkApp = sink.Install (nodes.Get (1));
  sinkApp.Start (Seconds (0.0));
  sinkApp.Stop (Seconds (simTime));

  // Sender
  BulkSendHelper source ("ns3::TcpSocketFactory",
                         InetSocketAddress (interfaces.GetAddress (1), port));

  source.SetAttribute ("MaxBytes", UintegerValue (0));

  ApplicationContainer sourceApp = source.Install (nodes.Get (0));
  sourceApp.Start (Seconds (1.0));
  sourceApp.Stop (Seconds (simTime));

  // NetAnim
  AnimationInterface anim("reno.xml");

  Simulator::Stop (Seconds (simTime));

  Simulator::Run ();

  // 🔹 Print results
  Ptr<PacketSink> sink1 = DynamicCast<PacketSink>(sinkApp.Get(0));
  uint64_t bytes = sink1->GetTotalRx();

  double throughput = bytes * 8.0 / simTime / 1000000;

  std::cout << "===== TCP NewReno Results =====" << std::endl;
  std::cout << "Bytes Received: " << bytes << std::endl;
  std::cout << "Throughput: " << throughput << " Mbps" << std::endl;

  Simulator::Destroy ();
}

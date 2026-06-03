#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

using namespace ns3;

int main ()
{
  // Use TCP NewReno
  Config::SetDefault ("ns3::TcpL4Protocol::SocketType",
                      TypeIdValue (TcpNewReno::GetTypeId()));

  // Create 2 nodes
  NodeContainer nodes;
  nodes.Create (2);

  // Create point-to-point link
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer devices = p2p.Install (nodes);
  p2p.EnablePcapAll("tcp-sim");

  // Install TCP/IP stack
  InternetStackHelper stack;
  stack.Install (nodes);

  // Assign IP
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  // TCP receiver (server)
  uint16_t port = 8080;
  PacketSinkHelper sink ("ns3::TcpSocketFactory",
                         InetSocketAddress (Ipv4Address::GetAny (), port));

  ApplicationContainer sinkApp = sink.Install (nodes.Get (1));
  sinkApp.Start (Seconds (0.0));
  sinkApp.Stop (Seconds (10.0));

  // TCP sender (client)
  BulkSendHelper source ("ns3::TcpSocketFactory",
                         InetSocketAddress (interfaces.GetAddress (1), port));

  source.SetAttribute ("MaxBytes", UintegerValue (0)); // unlimited

  ApplicationContainer sourceApp = source.Install (nodes.Get (0));
  sourceApp.Start (Seconds (1.0));
  sourceApp.Stop (Seconds (10.0));

  Simulator::Stop (Seconds (10.0));
  LogComponentEnable("BulkSendApplication", LOG_LEVEL_INFO);
  LogComponentEnable("PacketSink", LOG_LEVEL_INFO);

  Simulator::Run ();
  Simulator::Destroy ();

  std::cout << "TCP simulation finished successfully!" << std::endl;
}

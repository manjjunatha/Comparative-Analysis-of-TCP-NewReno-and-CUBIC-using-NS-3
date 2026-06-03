#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include "ns3/error-model.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TcpResearch");

// -------- CWND TRACE FUNCTION --------
static void
CwndTracer (Ptr<OutputStreamWrapper> stream,
            uint32_t oldCwnd,
            uint32_t newCwnd)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds ()
                        << "\t" << newCwnd << std::endl;
}

int main (int argc, char *argv[])
{
  std::string tcpType = "NewReno";
  std::string bandwidth = "10Mbps";
  std::string delay = "10ms";
  double lossRate = 0.0;

  CommandLine cmd;
  cmd.AddValue ("tcpType", "TCP variant: NewReno or Cubic", tcpType);
  cmd.AddValue ("bandwidth", "Link bandwidth", bandwidth);
  cmd.AddValue ("delay", "Link delay", delay);
  cmd.AddValue ("lossRate", "Packet loss rate (0.01 = 1%)", lossRate);
  cmd.Parse (argc, argv);

  // -------- SELECT TCP --------
  if (tcpType == "Cubic")
  {
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType",
                        TypeIdValue (TcpCubic::GetTypeId ()));
  }
  else
  {
    Config::SetDefault ("ns3::TcpL4Protocol::SocketType",
                        TypeIdValue (TcpNewReno::GetTypeId ()));
  }

  // -------- PROPER MSS --------
  Config::SetDefault ("ns3::TcpSocket::SegmentSize",
                      UintegerValue (1448));

  // -------- CREATE NODES --------
  NodeContainer nodes;
  nodes.Create (2);

  // -------- LINK CONFIG --------
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue (bandwidth));
  p2p.SetChannelAttribute ("Delay", StringValue (delay));

  // Increase queue size to avoid artificial bottleneck
  p2p.SetQueue ("ns3::DropTailQueue",
                "MaxSize", StringValue ("1000p"));

  NetDeviceContainer devices = p2p.Install (nodes);

  // -------- PACKET LOSS (PACKET-BASED) --------
  if (lossRate > 0.0)
  {
    Ptr<RateErrorModel> em = CreateObject<RateErrorModel> ();
    em->SetAttribute ("ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
    em->SetAttribute ("ErrorRate", DoubleValue (lossRate));
    devices.Get (1)->SetAttribute ("ReceiveErrorModel",
                                   PointerValue (em));
  }

  // -------- INTERNET STACK --------
  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  // -------- APPLICATIONS --------
  uint16_t port = 8080;

  // Receiver
  PacketSinkHelper sink ("ns3::TcpSocketFactory",
                         InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApp = sink.Install (nodes.Get (1));
  sinkApp.Start (Seconds (0.0));
  sinkApp.Stop (Seconds (100.0));

  // Sender (continuous TCP)
  BulkSendHelper source ("ns3::TcpSocketFactory",
                         InetSocketAddress (interfaces.GetAddress (1), port));

  source.SetAttribute ("MaxBytes", UintegerValue (0));
  ApplicationContainer sourceApp = source.Install (nodes.Get (0));
  sourceApp.Start (Seconds (1.0));
  sourceApp.Stop (Seconds (100.0));

  // -------- CWND TRACE --------
  AsciiTraceHelper ascii;
  Ptr<OutputStreamWrapper> stream =
      ascii.CreateFileStream ("cwnd.txt");

  Simulator::Schedule (Seconds (1.01), [&stream](){
    Config::ConnectWithoutContext (
      "/NodeList/0/$ns3::TcpL4Protocol/SocketList/*/CongestionWindow",
      MakeBoundCallback (&CwndTracer, stream));
  });

  // -------- PCAP --------
  p2p.EnablePcapAll ("tcp");

  // -------- NETANIM --------
  AnimationInterface anim ("anim.xml");

  // -------- FLOWMONITOR --------
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Stop (Seconds (100.0));
  Simulator::Run ();

  monitor->SerializeToXmlFile ("flow.xml", true, true);

  Simulator::Destroy ();
  return 0;
}

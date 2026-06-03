#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FiveGLteComparison");

int main (int argc, char *argv[])
{

    double simTime = 30.0;

    // Open output file
    std::ofstream out("lte_results.txt");

    // Header for graph plotting
    out << "Users Throughput Delay PacketLoss" << std::endl;

    uint16_t userSet[] = {5,10,15,20};

    for(int u = 0; u < 4; u++)
    {

        uint16_t numUe = userSet[u];

        NodeContainer ueNodes;
        NodeContainer enbNodes;

        ueNodes.Create(numUe);
        enbNodes.Create(1);

        Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
        Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();

        lteHelper->SetEpcHelper(epcHelper);

        Ptr<Node> pgw = epcHelper->GetPgwNode();

        InternetStackHelper internet;
        internet.Install(ueNodes);

        MobilityHelper mobility;

        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(enbNodes);

        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(ueNodes);

        NetDeviceContainer enbDevs =
        lteHelper->InstallEnbDevice(enbNodes);

        NetDeviceContainer ueDevs =
        lteHelper->InstallUeDevice(ueNodes);

        for(uint32_t i=0;i<numUe;i++)
        {
            lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(0));
        }

        Ipv4InterfaceContainer ueIp;
        ueIp = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));

        Ipv4StaticRoutingHelper ipv4RoutingHelper;

        for(uint32_t i=0;i<numUe;i++)
        {
            Ptr<Ipv4StaticRouting> ueStaticRouting =
            ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(i)->GetObject<Ipv4>());

            ueStaticRouting->SetDefaultRoute(
            epcHelper->GetUeDefaultGatewayAddress(),1);
        }

        uint16_t port = 4000;

        UdpServerHelper server(port);
        ApplicationContainer serverApp =
        server.Install(ueNodes.Get(0));

        serverApp.Start(Seconds(1.0));
        serverApp.Stop(Seconds(simTime));

        UdpClientHelper client(ueIp.GetAddress(0), port);

        client.SetAttribute("Interval", TimeValue(MilliSeconds(1)));
        client.SetAttribute("PacketSize", UintegerValue(1400));
        client.SetAttribute("MaxPackets", UintegerValue(1000000));

        ApplicationContainer clientApps;

        for(uint32_t i=1;i<numUe;i++)
        {
            clientApps.Add(client.Install(ueNodes.Get(i)));
        }

        clientApps.Start(Seconds(2.0));
        clientApps.Stop(Seconds(simTime));

        FlowMonitorHelper flowmon;
        Ptr<FlowMonitor> monitor = flowmon.InstallAll();

        Simulator::Stop(Seconds(simTime));
        Simulator::Run();

        monitor->CheckForLostPackets();

        std::map<FlowId, FlowMonitor::FlowStats> stats =
        monitor->GetFlowStats();

        double totalThroughput = 0;
        double totalDelay = 0;
        double totalLoss = 0;
        int flowCount = 0;

        for(auto &flow : stats)
        {

            double duration =
            flow.second.timeLastRxPacket.GetSeconds() -
            flow.second.timeFirstTxPacket.GetSeconds();

            if(duration <= 0) continue;

            double throughput =
            flow.second.rxBytes * 8.0 / duration / 1024 / 1024;

            double delay =
            flow.second.delaySum.GetSeconds() /
            flow.second.rxPackets;

            double loss =
            (flow.second.lostPackets * 100.0) /
            flow.second.txPackets;

            totalThroughput += throughput;
            totalDelay += delay;
            totalLoss += loss;

            flowCount++;

        }

        double finalThroughput = totalThroughput / flowCount;
        double finalDelay = totalDelay / flowCount;
        double finalLoss = totalLoss / flowCount;

        std::cout << "Users: " << numUe << std::endl;
        std::cout << "Throughput: " << finalThroughput << " Mbps" << std::endl;
        std::cout << "Delay: " << finalDelay << " s" << std::endl;
        std::cout << "Packet Loss: " << finalLoss << " %" << std::endl;
        std::cout << "---------------------------------" << std::endl;

        // Save results in file for graph plotting
        out << numUe << " "
            << finalThroughput << " "
            << finalDelay << " "
            << finalLoss << std::endl;

        Simulator::Destroy();

    }

    out.close();

}

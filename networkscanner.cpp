#include "networkscanner.h"
#include <QDebug>
#include <QThread>

using namespace Crafter;

// Initialize static member
NetworkScanner* NetworkScanner::s_instance = nullptr;

NetworkScanner::NetworkScanner(QObject *parent) : QObject(parent) {
    s_instance = this;
}

void NetworkScanner::onPacketReceived(Packet* packet, void* user) {
    // If user is provided (from Capture), use it. Otherwise fallback to static instance.
    NetworkScanner* scanner = user ? static_cast<NetworkScanner*>(user) : s_instance;
    if (!scanner) return;
    
    ARP* recv_arp = packet->GetLayer<ARP>();
    if (recv_arp && recv_arp->GetOperation() == ARP::Reply) {
        Device dev;
        dev.ip = QString::fromStdString(recv_arp->GetSenderIP());
        dev.mac = QString::fromStdString(recv_arp->GetSenderMAC());
        dev.hostname = "Unknown";
        dev.upBandwidth = "0 KB/s";
        dev.downBandwidth = "0 KB/s";
        dev.status = "Online";
        dev.vendor = scanner->getMacVendor(dev.mac);
        
        scanner->addDiscoveredDevice(dev);
    }
}

void NetworkScanner::addDiscoveredDevice(const Device &dev) {
    for (const auto& d : m_discoveredThisScan) {
        if (d.ip == dev.ip) return;
    }
    m_discoveredThisScan.append(dev);
}

void NetworkScanner::runScan() {
    QString iface = getActiveInterface();
    if (iface.isEmpty()) {
        emit scanError("No active network interface found.");
        return;
    }

    emit statusMessage("Scanning network on " + iface + "...");

    std::string interface = iface.toStdString();
    std::string ip_addr = GetMyIP(interface);
    
    if (ip_addr.empty()) {
        emit scanError("Could not retrieve IP address for " + iface);
        return;
    }

    QString baseIP = QString::fromStdString(ip_addr).section('.', 0, 2) + ".";
    m_discoveredThisScan.clear();

    // The Sniffer constructor in this version of libcrafter takes only 3 arguments
    Sniffer sniffer("arp and dst host " + ip_addr, interface, onPacketReceived);

    ARP arp_pck;
    arp_pck.SetOperation(ARP::Request);
    arp_pck.SetSenderIP(ip_addr);
    arp_pck.SetSenderMAC(GetMyMAC(interface));

    Ethernet eth_pck;
    eth_pck.SetSourceMAC(GetMyMAC(interface));
    eth_pck.SetDestinationMAC("ff:ff:ff:ff:ff:ff");

    Packet arp_request = eth_pck / arp_pck;

    for (int i = 1; i < 255; ++i) {
        QString target = baseIP + QString::number(i);
        arp_pck.SetTargetIP(target.toStdString());
        arp_request.Send(interface);
    }

    // Capture responses - passing 'this' as the user context to the callback
    sniffer.Capture(10, this); 

    emit devicesDiscovered(m_discoveredThisScan);
    emit statusMessage(QString::number(m_discoveredThisScan.size()) + " devices found on " + iface);
}

#include <QNetworkInterface>

QString NetworkScanner::getActiveInterface() {
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        // Check if the interface is up and not a loopback
        if (interface.flags().testFlag(QNetworkInterface::IsUp) &&
            !interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            
            // Search for an IPv4 address
            for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    return interface.name();
                }
            }
        }
    }
    return "";
}

QString NetworkScanner::getMacVendor(const QString &mac) {
    if (mac.startsWith("00:50:56", Qt::CaseInsensitive)) return "VMware";
    if (mac.startsWith("A4:D1:8C", Qt::CaseInsensitive)) return "Apple";
    return "Unknown Vendor";
}

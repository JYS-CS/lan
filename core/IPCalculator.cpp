#include "IPCalculator.h"
#include <QNetworkAddressEntry>
#include <QDebug>
#include <bitset>
#include <algorithm>

namespace core {

NetworkInfo IPCalculator::calculate(const QString &ip, int prefix) {
    NetworkInfo info;
    QHostAddress addr(ip);
    if (addr.isNull()) return info;

    info.isValid = true;
    bool isIPv4 = (addr.protocol() == QAbstractSocket::IPv4Protocol);
    info.ipVersion = isIPv4 ? "IPv4" : "IPv6";
    info.ipClass = getIpClass(addr);
    info.isPrivate = IPCalculator::isPrivate(ip);
    info.isPublic = !info.isPrivate && getReservedType(ip).isEmpty();
    info.reservedType = getReservedType(ip);
    info.binaryRepresentation = toBinary(ip);
    info.hexRepresentation = toHex(ip);

    if (isIPv4) {
        uint32_t address = addr.toIPv4Address();
        uint32_t mask = (prefix == 0) ? 0 : (0xFFFFFFFF << (32 - prefix));
        uint32_t network = address & mask;
        uint32_t broadcast = network | (~mask);

        info.networkAddress = QHostAddress(network).toString();
        info.broadcastAddress = QHostAddress(broadcast).toString();
        info.firstHost = QHostAddress(network + 1).toString();
        info.lastHost = QHostAddress(broadcast - 1).toString();
        
        info.totalHosts = std::pow(2, 32 - prefix);
        info.usableHosts = std::max(0LL, info.totalHosts - 2);
        info.cidrNotation = QString("%1/%2").arg(info.networkAddress).arg(prefix);
        info.subnetMask = QHostAddress(mask).toString();
    } else {
        // Simple IPv6 support (Network Address / CIDR)
        // Full IPv6 math would require 128-bit backend, providing essential CIDR for now
        info.cidrNotation = QString("%1/%2").arg(addr.toString()).arg(prefix);
        info.networkAddress = addr.toString(); // simplistic
        info.totalHosts = -1; // overflow for 64-bit long long
        info.usableHosts = -1;
    }

    return info;
}

std::vector<QString> IPCalculator::createSubnets(const QString &network, int currentPrefix, int subnetCount) {
    std::vector<QString> subnets;
    QHostAddress addr(network);
    if (addr.isNull() || addr.protocol() != QAbstractSocket::IPv4Protocol) return subnets;

    int newBits = std::ceil(std::log2(subnetCount));
    int newPrefix = currentPrefix + newBits;
    if (newPrefix > 32) return subnets;

    uint32_t address = addr.toIPv4Address();
    uint32_t subnetSize = std::pow(2, 32 - newPrefix);

    for (int i = 0; i < subnetCount; ++i) {
        uint32_t subNet = address + (i * subnetSize);
        subnets.push_back(QString("%1/%2").arg(QHostAddress(subNet).toString()).arg(newPrefix));
    }
    return subnets;
}

QString IPCalculator::aggregate(const QStringList &subnets) {
    if (subnets.isEmpty()) return "";
    // Simplified CIDR aggregation for IPv4
    uint32_t minAddr = 0xFFFFFFFF;
    uint32_t maxAddr = 0x00000000;
    
    for (const QString &s : subnets) {
        QString clean = s.split("/")[0];
        QHostAddress a(clean);
        if (a.protocol() == QAbstractSocket::IPv4Protocol) {
            uint32_t val = a.toIPv4Address();
            minAddr = std::min(minAddr, val);
            maxAddr = std::max(maxAddr, val);
        }
    }

    uint32_t diff = minAddr ^ maxAddr;
    int commonPrefix = 32;
    if (diff != 0) {
        commonPrefix = 31 - std::floor(std::log2(diff));
    }

    uint32_t mask = (commonPrefix == 0) ? 0 : (0xFFFFFFFF << (32 - commonPrefix));
    return QString("%1/%2").arg(QHostAddress(minAddr & mask).toString()).arg(commonPrefix);
}

bool IPCalculator::isValidIp(const QString &ip) {
    return !QHostAddress(ip).isNull();
}

QString IPCalculator::getIpVersion(const QString &ip) {
    QHostAddress addr(ip);
    if (addr.isNull()) return "Unknown";
    return (addr.protocol() == QAbstractSocket::IPv4Protocol) ? "IPv4" : "IPv6";
}

bool IPCalculator::isPrivate(const QString &ip) {
    QHostAddress addr(ip);
    if (addr.isNull()) return false;
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        uint32_t v = addr.toIPv4Address();
        return (v >= 0x0A000000 && v <= 0x0AFFFFFF) || // 10.0.0.0/8
               (v >= 0xAC100000 && v <= 0xAC1FFFFF) || // 172.16.0.0/12
               (v >= 0xC0A80000 && v <= 0xC0A8FFFF);   // 192.168.0.0/16
    }
    return addr.toString().startsWith("fc") || addr.toString().startsWith("fd");
}

QString IPCalculator::getReservedType(const QString &ip) {
    QHostAddress addr(ip);
    if (addr.isLoopback()) return "Loopback";
    if (addr.isMulticast()) return "Multicast";
    // Simplified reserved types
    return "";
}

QString IPCalculator::toBinary(const QString &ip) {
    QHostAddress addr(ip);
    if (addr.isNull() || addr.protocol() != QAbstractSocket::IPv4Protocol) return "";
    uint32_t v = addr.toIPv4Address();
    std::bitset<32> b(v);
    QString s = QString::fromStdString(b.to_string());
    return QString("%1.%2.%3.%4").arg(s.mid(0,8)).arg(s.mid(8,8)).arg(s.mid(16,8)).arg(s.mid(24,8));
}

QString IPCalculator::toHex(const QString &ip) {
    QHostAddress addr(ip);
    if (addr.isNull() || addr.protocol() != QAbstractSocket::IPv4Protocol) return "";
    return "0x" + QString::number(addr.toIPv4Address(), 16).toUpper();
}

QString IPCalculator::getIpClass(const QHostAddress &addr) {
    if (addr.protocol() != QAbstractSocket::IPv4Protocol) return "N/A";
    uint8_t first = (addr.toIPv4Address() >> 24) & 0xFF;
    if (first >= 1 && first <= 126) return "A";
    if (first >= 128 && first <= 191) return "B";
    if (first >= 192 && first <= 223) return "C";
    if (first >= 224 && first <= 239) return "D (Multicast)";
    if (first >= 240) return "E (Reserved)";
    return "Unknown";
}

std::vector<LocalInterface> IPCalculator::getLocalInterfaces() {
    std::vector<LocalInterface> results;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags().testFlag(QNetworkInterface::IsUp)) || 
            iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;

        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                LocalInterface li;
                li.name = iface.humanReadableName();
                li.ip = entry.ip().toString();
                li.mask = entry.netmask().toString();
                li.prefix = entry.prefixLength();
                results.push_back(li);
            }
        }
    }
    return results;
}

QStringList IPCalculator::generateRange(const QString &firstHost, const QString &lastHost, int maxCount) {
    QStringList range;
    QHostAddress start(firstHost);
    QHostAddress end(lastHost);
    if (start.isNull() || end.isNull() || start.protocol() != QAbstractSocket::IPv4Protocol) return range;

    uint32_t s = start.toIPv4Address();
    uint32_t e = end.toIPv4Address();

    if (s > e) return range;
    
    uint32_t count = e - s + 1;
    if (count > (uint32_t)maxCount) count = (uint32_t)maxCount;

    for (uint32_t i = 0; i < count; ++i) {
        range << QHostAddress(s + i).toString();
    }
    return range;
}

} // namespace core

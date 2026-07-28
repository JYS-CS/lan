#pragma once

#include <QString>
#include <QStringList>
#include <QHostAddress>
#include <vector>
#include <cmath>

namespace core {

struct NetworkInfo {
    bool isValid = false;
    QString ipVersion; // IPv4 or IPv6
    QString networkAddress;
    QString broadcastAddress; // null for IPv6
    QString firstHost;
    QString lastHost;
    long long totalHosts;
    long long usableHosts;
    QString cidrNotation;
    QString subnetMask;
    QString ipClass;
    bool isPrivate = false;
    bool isPublic = false;
    QString reservedType;
    QString binaryRepresentation;
    QString hexRepresentation;
};

struct LocalInterface {
    QString name;
    QString ip;
    QString mask;
    int prefix;
};

class IPCalculator {
public:
    static NetworkInfo calculate(const QString &ip, int prefix);
    static std::vector<QString> createSubnets(const QString &network, int currentPrefix, int subnetCount);
    static QString aggregate(const QStringList &subnets);
    
    // New Pro Features
    static std::vector<LocalInterface> getLocalInterfaces();
    static QStringList generateRange(const QString &firstHost, const QString &lastHost, int maxCount = 65536);

    // Helpers
    static bool isValidIp(const QString &ip);
    static QString getIpVersion(const QString &ip);
    static bool isPrivate(const QString &ip);
    static QString getReservedType(const QString &ip);
    static QString toBinary(const QString &ip);
    static QString toHex(const QString &ip);

private:
    static QString getIpClass(const QHostAddress &addr);
    static QString calculateIPv4Subnet(uint32_t address, int prefix);
};

} // namespace core

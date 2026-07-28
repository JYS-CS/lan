#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <crafter.h>

struct Device {
    QString ip;
    QString mac;
    QString hostname;
    QString upBandwidth;
    QString downBandwidth;
    QString status;
    QString vendor;
};

class NetworkScanner : public QObject {
    Q_OBJECT

public:
    explicit NetworkScanner(QObject *parent = nullptr);
    ~NetworkScanner() override = default;

public slots:
    void runScan();

signals:
    void devicesDiscovered(const QList<Device> &devices);
    void scanError(const QString &message);
    void statusMessage(const QString &message);

private:
    static void onPacketReceived(Crafter::Packet* packet, void* user);
    void addDiscoveredDevice(const Device &dev);

    QString getActiveInterface();
    QString getLocalIP(const QString &iface);
    QString getMacVendor(const QString &mac);

    QList<Device> m_discoveredThisScan;
    static NetworkScanner* s_instance;
};

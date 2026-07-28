#pragma once

#include <QObject>
#include <QString>
#include <atomic>

// Forward declare pcap types
struct pcap;
typedef struct pcap pcap_t;
struct pcap_pkthdr;

namespace core {

class PacketCapture : public QObject {
    Q_OBJECT

public:
    explicit PacketCapture(const QString &interface, QObject *parent = nullptr);
    ~PacketCapture() override;

public slots:
    void startCapture();
    void stopCapture();

signals:
    void packetCaptured(const unsigned char* pkt, int len);
    void captureError(const QString &message);
    void captureStarted(const QString &interface);
    void captureStopped();

private:
    static void onPacketReceived(unsigned char* user, const struct pcap_pkthdr* h, const unsigned char* pkt);
    
    QString m_interface;
    std::atomic<bool> m_running;
    pcap_t* m_handle = nullptr;
};

} // namespace core

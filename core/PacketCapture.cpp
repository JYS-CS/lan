#include "PacketCapture.h"
#include <QDebug>
#include <pcap/pcap.h>

namespace core {

PacketCapture::PacketCapture(const QString &interface, QObject *parent)
    : QObject(parent), m_interface(interface), m_running(false) {
}

PacketCapture::~PacketCapture() {
    stopCapture();
}

void PacketCapture::startCapture() {
    if (m_running) return;

    char errbuf[PCAP_ERRBUF_SIZE];
    m_handle = pcap_create(m_interface.toUtf8().constData(), errbuf);
    
    if (!m_handle) {
        emit captureError(QString("pcap_create Error: %1").arg(errbuf));
        return;
    }

    // Set configuration
    pcap_set_snaplen(m_handle, 65535);
    pcap_set_promisc(m_handle, 1);
    pcap_set_timeout(m_handle, 10); // low latency chunking
    
    // [CRITICAL FIX] Increase capture buffer size to 32MB to prevent the kernel 
    // from dropping packets during high-speed local downloads (gigabit+ Wi-Fi)
    pcap_set_buffer_size(m_handle, 32 * 1024 * 1024);

    int status = pcap_activate(m_handle);
    if (status != 0) {
        emit captureError(QString("pcap_activate failed: %1").arg(pcap_geterr(m_handle)));
        pcap_close(m_handle);
        m_handle = nullptr;
        return;
    }

    m_running = true;
    emit captureStarted(m_interface);

    while (m_running) {
        int res = pcap_dispatch(m_handle, -1, onPacketReceived, (unsigned char*)this);
        if (res < 0) break;
    }

    pcap_close(m_handle);
    m_handle = nullptr;
    m_running = false;
    emit captureStopped();
}

void PacketCapture::stopCapture() {
    m_running = false;
    if (m_handle) {
        pcap_breakloop(m_handle);
    }
}

void PacketCapture::onPacketReceived(unsigned char* user, const struct pcap_pkthdr* h, const unsigned char* pkt) {
    PacketCapture* capturer = (PacketCapture*)user;
    if (!capturer || !capturer->m_running) return;

    emit capturer->packetCaptured(pkt, h->caplen);
}

} // namespace core

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
    m_handle = pcap_open_live(m_interface.toUtf8().constData(), BUFSIZ, 1, 1000, errbuf);
    
    if (!m_handle) {
        emit captureError(QString("pcap Error: %1").arg(errbuf));
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

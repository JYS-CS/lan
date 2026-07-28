#include "DnsResponder.h"
#include <QDebug>
#include <QDataStream>
#include <QNetworkDatagram>

namespace core {

DnsResponder::DnsResponder(QObject *parent) : QObject(parent) {
    m_socket = new QUdpSocket(this);
}

DnsResponder::~DnsResponder() {
    stop();
}

bool DnsResponder::start(const QString &targetIp) {
    if (m_running) stop();
    
    m_targetIp = QHostAddress(targetIp);
    
    // Bind to all interfaces on port 53
    if (!m_socket->bind(QHostAddress::AnyIPv4, 53, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "[DnsResponder] Failed to bind to port 53:" << m_socket->errorString();
        return false;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &DnsResponder::processPendingDatagrams);
    m_running = true;
    qDebug() << "[DnsResponder] Listening on port 53. Redirecting all queries to" << targetIp;
    return true;
}

void DnsResponder::stop() {
    if (!m_running) return;
    m_socket->close();
    m_running = false;
    disconnect(m_socket, &QUdpSocket::readyRead, this, &DnsResponder::processPendingDatagrams);
}

void DnsResponder::processPendingDatagrams() {
    while (m_socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        QByteArray request = datagram.data();
        
        if (request.size() < 12) continue; // Invalid DNS header

        QByteArray response = buildResponse(request);
        if (!response.isEmpty()) {
            m_socket->writeDatagram(response, datagram.senderAddress(), datagram.senderPort());
        }
    }
}

QByteArray DnsResponder::buildResponse(const QByteArray &request) {
    // DNS Header Ref: RFC 1035
    // ID (2 bytes)
    // Flags (2 bytes) -> 0x8180 (Standard query response, No error)
    // QDCOUNT (2 bytes) -> 1
    // ANCOUNT (2 bytes) -> 1
    // NSCOUNT (2 bytes) -> 0
    // ARCOUNT (2 bytes) -> 0

    QByteArray response;
    QDataStream out(&response, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian);

    // Copy ID from request
    uint16_t id = *reinterpret_cast<const uint16_t*>(request.constData());
    out << id;
    
    // Flags: Response, OpCode 0, AA 0, TC 0, RD 1 | RA 1, Z 0, RCODE 0
    out << (uint16_t)0x8180;
    
    // Counts
    out << (uint16_t)1; // Questions
    out << (uint16_t)1; // Answers
    out << (uint16_t)0; // Authority
    out << (uint16_t)0; // Additional

    // 1. Question Section (Copy from request)
    // The question section ends after the first QTYPE/QCLASS after the name.
    // Minimal parser to find the end of the question:
    int pos = 12; // Skip header
    while (pos < request.size() && request[pos] != 0) {
        pos += (uint8_t)request[pos] + 1;
    }
    pos += 5; // Skip null terminator + QTYPE(2) + QCLASS(2)
    
    if (pos > request.size()) return QByteArray(); // Malformed
    
    response.append(request.mid(12, pos - 12));
    
    // 2. Answer Section
    // Pointer to name (offset 12)
    out.device()->seek(response.size());
    out << (uint16_t)0xc00c; // Compression pointer to name at offset 12
    out << (uint16_t)1;      // Type A
    out << (uint16_t)1;      // Class IN
    out << (uint32_t)60;     // TTL 60s
    out << (uint16_t)4;      // Data length 4
    
    // IP Address
    uint32_t ip = m_targetIp.toIPv4Address();
    out << ip;

    return response;
}

} // namespace core

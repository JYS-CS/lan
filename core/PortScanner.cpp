#include "PortScanner.h"
#include <QHostAddress>

namespace core {

PortScanner::PortScanner(const QString &ip, QObject *parent)
    : QObject(parent), m_targetIp(ip) {
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &PortScanner::onConnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &PortScanner::onError);
}

void PortScanner::start(const QList<int> &ports) {
    m_portsToScan = ports;
    m_currentIndex = 0;
    scanNext();
}

void PortScanner::scanNext() {
    if (m_currentIndex >= m_portsToScan.size()) {
        emit finished();
        return;
    }

    emit progress(m_currentIndex, m_portsToScan.size());
    
    int port = m_portsToScan[m_currentIndex];
    m_socket->abort();
    m_socket->connectToHost(m_targetIp, port);
    
    // Timeout for each port check
    QTimer::singleShot(200, this, [this, port]() {
        if (m_socket->state() == QAbstractSocket::ConnectingState) {
            m_socket->abort();
            onError(); // Treat timeout as closed
        }
    });
}

void PortScanner::onConnected() {
    PortResult res;
    res.port = m_portsToScan[m_currentIndex];
    res.isOpen = true;
    res.service = getServiceName(res.port);
    
    emit resultFound(res);
    m_currentIndex++;
    scanNext();
}

void PortScanner::onError() {
    m_currentIndex++;
    scanNext();
}

QString PortScanner::getServiceName(int port) {
    switch (port) {
        case 21:   return "FTP";
        case 22:   return "SSH";
        case 23:   return "Telnet";
        case 25:   return "SMTP";
        case 53:   return "DNS";
        case 80:   return "HTTP";
        case 110:  return "POP3";
        case 139:  return "NetBIOS";
        case 443:  return "HTTPS";
        case 445:  return "SMB";
        case 1433: return "MSSQL";
        case 3306: return "MySQL";
        case 3389: return "RDP";
        case 5432: return "PostgreSQL";
        case 8080: return "HTTP-Alt";
        default:   return "Unknown";
    }
}

} // namespace core

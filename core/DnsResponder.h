#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>

namespace core {

class DnsResponder : public QObject {
    Q_OBJECT
public:
    explicit DnsResponder(QObject *parent = nullptr);
    ~DnsResponder();

    bool start(const QString &targetIp);
    void stop();
    bool isRunning() const { return m_running; }

private slots:
    void processPendingDatagrams();

private:
    QUdpSocket *m_socket = nullptr;
    QHostAddress m_targetIp;
    bool m_running = false;

    // Minimal DNS packet processing
    QByteArray buildResponse(const QByteArray &request);
};

} // namespace core

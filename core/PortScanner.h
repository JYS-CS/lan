#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QList>
#include "Types.h"

namespace core {

class PortScanner : public QObject {
    Q_OBJECT

public:
    explicit PortScanner(const QString &ip, QObject *parent = nullptr);
    void start(const QList<int> &ports);

signals:
    void progress(int current, int total);
    void resultFound(const core::PortResult &res);
    void finished();

private slots:
    void scanNext();
    void onConnected();
    void onError();

private:
    QString m_targetIp;
    QList<int> m_portsToScan;
    int m_currentIndex = 0;
    QTcpSocket *m_socket = nullptr;
    
    QString getServiceName(int port);
};

} // namespace core

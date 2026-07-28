#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSslServer>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>

namespace core {

class BlockHttpServer : public QObject {
    Q_OBJECT
public:
    explicit BlockHttpServer(QObject *parent = nullptr);
    ~BlockHttpServer();

    bool start();
    void stop();
    bool isRunning() const { return m_running; }

private slots:
    void onNewConnection();
    void onNewSslConnection();
    void onReadyRead();
    void onSslErrors(const QList<QSslError> &errors);

private:
    QTcpServer *m_server      = nullptr;
    QSslServer *m_httpsServer = nullptr;
    bool m_running = false;

    void setupSsl();
    QString buildHtmlPage();
};

} // namespace core

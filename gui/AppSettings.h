#pragma once
#include <QObject>
#include <QSettings>

namespace gui {

class AppSettings : public QObject {
    Q_OBJECT
public:
    static AppSettings* instance();

    bool showSparklines()    const { return m_showSparklines; }
    bool showUploadColumn()  const { return m_showUpload; }
    bool showDownloadColumn() const { return m_showDownload; }
    bool autoClearHistoricalDevices() const { return m_autoClear; }
    bool blockNewDevicesByDefault() const { return m_blockNewDevices; }
    bool dnsVisibilityEnabled() const { return m_dnsVisibility; }

public slots:
    void setShowSparklines(bool v);
    void setShowUploadColumn(bool v);
    void setShowDownloadColumn(bool v);
    void setAutoClearHistoricalDevices(bool v);
    void setBlockNewDevicesByDefault(bool v);
    void setDnsVisibilityEnabled(bool v);

signals:
    void settingsChanged();

private:
    explicit AppSettings(QObject *parent = nullptr);
    bool m_showSparklines = true;
    bool m_showUpload     = true;
    bool m_showDownload   = true;
    bool m_autoClear      = false;
    bool m_blockNewDevices = false;
    bool m_dnsVisibility = false;
    QSettings m_qs;
};

} // namespace gui

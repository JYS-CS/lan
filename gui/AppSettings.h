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

public slots:
    void setShowSparklines(bool v);
    void setShowUploadColumn(bool v);
    void setShowDownloadColumn(bool v);

signals:
    void settingsChanged();

private:
    explicit AppSettings(QObject *parent = nullptr);
    bool m_showSparklines = true;
    bool m_showUpload     = true;
    bool m_showDownload   = true;
    QSettings m_qs;
};

} // namespace gui

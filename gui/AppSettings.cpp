#include "AppSettings.h"

namespace gui {

AppSettings* AppSettings::instance() {
    static AppSettings s_instance;
    return &s_instance;
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent), m_qs("LAN-Monitor", "AppPrefs")
{
    m_showSparklines = m_qs.value("showSparklines", true).toBool();
    m_showUpload     = m_qs.value("showUpload",     true).toBool();
    m_showDownload   = m_qs.value("showDownload",   true).toBool();
}

void AppSettings::setShowSparklines(bool v) {
    if (m_showSparklines == v) return;
    m_showSparklines = v;
    m_qs.setValue("showSparklines", v);
    emit settingsChanged();
}

void AppSettings::setShowUploadColumn(bool v) {
    if (m_showUpload == v) return;
    m_showUpload = v;
    m_qs.setValue("showUpload", v);
    emit settingsChanged();
}

void AppSettings::setShowDownloadColumn(bool v) {
    if (m_showDownload == v) return;
    m_showDownload = v;
    m_qs.setValue("showDownload", v);
    emit settingsChanged();
}

} // namespace gui

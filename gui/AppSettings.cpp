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
    m_autoClear      = m_qs.value("autoClearHistoricalDevices", false).toBool();
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

void AppSettings::setAutoClearHistoricalDevices(bool v) {
    if (m_autoClear == v) return;
    m_autoClear = v;
    m_qs.setValue("autoClearHistoricalDevices", v);
    emit settingsChanged();
}

} // namespace gui

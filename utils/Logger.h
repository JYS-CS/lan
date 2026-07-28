#pragma once

#include <QString>
#include <QDebug>
#include <QDateTime>

namespace utils {

class Logger {
public:
    static void log(const QString &message) {
        qDebug() << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "] " << message;
    }
};

} // namespace utils

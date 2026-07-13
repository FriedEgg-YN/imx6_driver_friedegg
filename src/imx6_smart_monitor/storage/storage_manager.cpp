#include "storage/storage_manager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace imx6sm {

static bool writeFile(const QString &path, const QString &data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        *error = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream << data;
    if (stream.status() != QTextStream::Ok) {
        *error = QStringLiteral("write failed");
        return false;
    }

    return true;
}

StorageCheckResult StorageManager::checkRoot(const QString &rootPath) const
{
    StorageCheckResult result;
    result.rootPath = rootPath;

    if (rootPath.isEmpty()) {
        result.error = QStringLiteral("empty storage root");
        return result;
    }

    QDir root(rootPath);
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("cannot create root path");
        return result;
    }

    if (!root.mkpath(QStringLiteral("sessions")) || !root.mkpath(QStringLiteral("latest"))) {
        result.error = QStringLiteral("cannot create storage subdirectories");
        return result;
    }

    const QString sessionId = QStringLiteral("test-") +
                              QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    result.sessionPath = root.absoluteFilePath(QStringLiteral("sessions/") + sessionId);
    if (!QDir().mkpath(result.sessionPath + QStringLiteral("/frames"))) {
        result.error = QStringLiteral("cannot create test session");
        return result;
    }

    const QString now = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    QString error;
    if (!writeFile(result.sessionPath + QStringLiteral("/session.json"),
                   QStringLiteral("{\"session_id\":\"%1\",\"start_time\":\"%2\",\"storage_version\":1}\n")
                       .arg(sessionId, now),
                   &error)) {
        result.error = QStringLiteral("session.json: %1").arg(error);
        return result;
    }

    if (!writeFile(result.sessionPath + QStringLiteral("/events.jsonl"),
                   QStringLiteral("{\"ts\":\"%1\",\"type\":\"storage_check\",\"value\":true}\n").arg(now),
                   &error)) {
        result.error = QStringLiteral("events.jsonl: %1").arg(error);
        return result;
    }

    if (!writeFile(result.sessionPath + QStringLiteral("/index.jsonl"),
                   QStringLiteral("{\"seq\":1,\"ts\":\"%1\",\"path\":\"frames/frame-000001.jpg\"}\n").arg(now),
                   &error)) {
        result.error = QStringLiteral("index.jsonl: %1").arg(error);
        return result;
    }

    if (!writeFile(root.absoluteFilePath(QStringLiteral("latest/status.json")),
                   QStringLiteral("{\"ts\":\"%1\",\"storage\":\"ok\"}\n").arg(now),
                   &error)) {
        result.error = QStringLiteral("latest/status.json: %1").arg(error);
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace imx6sm


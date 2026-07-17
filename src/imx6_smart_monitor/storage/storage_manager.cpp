#include "storage/storage_manager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace imx6sm {

static bool writeFile(const QString &path, const QString &data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream << data;
    if (stream.status() != QTextStream::Ok) {
        if (error)
            *error = QStringLiteral("write failed");
        return false;
    }

    return true;
}

static bool appendFile(const QString &path, const QString &data, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream << data;
    if (stream.status() != QTextStream::Ok) {
        if (error)
            *error = QStringLiteral("append failed");
        return false;
    }

    return true;
}

static QString jsonEscape(const QString &value)
{
    QString out;
    out.reserve(value.size() + 8);
    for (const QChar ch : value) {
        switch (ch.unicode()) {
        case '\\': out += QStringLiteral("\\\\"); break;
        case '"': out += QStringLiteral("\\\""); break;
        case '\b': out += QStringLiteral("\\b"); break;
        case '\f': out += QStringLiteral("\\f"); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        case '\t': out += QStringLiteral("\\t"); break;
        default:
            if (ch.unicode() < 0x20) {
                out += QStringLiteral("\\u%1").arg(ch.unicode(), 4, 16, QLatin1Char('0'));
            } else {
                out += ch;
            }
            break;
        }
    }
    return out;
}

static QString jsonString(const QString &value)
{
    return QStringLiteral("\"") + jsonEscape(value) + QStringLiteral("\"");
}

static QString timestampText()
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

static bool ensureDir(const QString &path, QString *error)
{
    if (path.isEmpty())
        return false;
    if (QDir().mkpath(path))
        return true;
    if (error)
        *error = QStringLiteral("cannot create %1").arg(path);
    return false;
}

static QString uniqueSessionId(const QString &rootPath)
{
    const QString base = QStringLiteral("camera-test-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString sessionId = base;
    for (int suffix = 2; suffix < 1000; ++suffix) {
        const QString sessionPath = QDir(rootPath).absoluteFilePath(QStringLiteral("sessions/") + sessionId);
        if (!QFileInfo::exists(sessionPath))
            return sessionId;
        sessionId = base + QStringLiteral("-%1").arg(suffix);
    }
    return base + QStringLiteral("-overflow");
}

static QString sessionRelativePath(const CameraTestSessionResult &session, const QString &absolutePath)
{
    return QDir(session.rootPath).relativeFilePath(absolutePath);
}

static CameraTestPathResult makeUniquePath(const QString &dirPath, const QString &baseName, const QString &suffix)
{
    CameraTestPathResult result;
    if (dirPath.isEmpty() || baseName.isEmpty() || suffix.isEmpty()) {
        result.error = QStringLiteral("invalid capture path request");
        return result;
    }

    if (!QDir().mkpath(dirPath)) {
        result.error = QStringLiteral("cannot create %1").arg(dirPath);
        return result;
    }

    for (int i = 0; i < 1000; ++i) {
        const QString candidateName = i == 0
            ? baseName + suffix
            : baseName + QStringLiteral("-%1").arg(i + 1) + suffix;
        const QString candidatePath = QDir(dirPath).absoluteFilePath(candidateName);
        if (!QFileInfo::exists(candidatePath)) {
            result.ok = true;
            result.path = candidatePath;
            result.relativePath = candidateName;
            return result;
        }
    }

    result.error = QStringLiteral("cannot allocate unique path in %1").arg(dirPath);
    return result;
}

static QString modeJson(const CameraMode &mode)
{
    return QStringLiteral("{\"fourcc\":%1,\"width\":%2,\"height\":%3,\"fps_num\":%4,\"fps_den\":%5,\"bytes_per_line\":%6,\"size_image\":%7}")
        .arg(jsonString(mode.fourcc))
        .arg(mode.width)
        .arg(mode.height)
        .arg(mode.fpsNum)
        .arg(mode.fpsDen)
        .arg(mode.bytesPerLine)
        .arg(mode.sizeImage);
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

    const QString sessionId = QStringLiteral("test-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    result.sessionPath = root.absoluteFilePath(QStringLiteral("sessions/") + sessionId);
    if (!QDir().mkpath(result.sessionPath + QStringLiteral("/frames"))) {
        result.error = QStringLiteral("cannot create test session");
        return result;
    }

    const QString now = timestampText();
    QString error;
    if (!writeFile(result.sessionPath + QStringLiteral("/session.json"),
                   QStringLiteral("{\"session_id\":%1,\"start_time\":%2,\"storage_version\":1}\n")
                       .arg(jsonString(sessionId), jsonString(now)),
                   &error)) {
        result.error = QStringLiteral("session.json: %1").arg(error);
        return result;
    }

    if (!writeFile(result.sessionPath + QStringLiteral("/events.jsonl"),
                   QStringLiteral("{\"ts\":%1,\"type\":\"storage_check\",\"value\":true}\n")
                       .arg(jsonString(now)),
                   &error)) {
        result.error = QStringLiteral("events.jsonl: %1").arg(error);
        return result;
    }

    if (!writeFile(result.sessionPath + QStringLiteral("/index.jsonl"),
                   QStringLiteral("{\"seq\":1,\"ts\":%1,\"path\":\"frames/frame-000001.jpg\"}\n")
                       .arg(jsonString(now)),
                   &error)) {
        result.error = QStringLiteral("index.jsonl: %1").arg(error);
        return result;
    }

    if (!writeFile(root.absoluteFilePath(QStringLiteral("latest/status.json")),
                   QStringLiteral("{\"ts\":%1,\"storage\":\"ok\"}\n").arg(jsonString(now)),
                   &error)) {
        result.error = QStringLiteral("latest/status.json: %1").arg(error);
        return result;
    }

    result.ok = true;
    return result;
}

CameraTestSessionResult StorageManager::openCameraTestSession(const QString &rootPath,
                                                              const QString &devicePath,
                                                              const CameraMode &mode) const
{
    CameraTestSessionResult result;
    result.rootPath = rootPath.trimmed();
    result.devicePath = devicePath.trimmed();
    result.mode = mode;

    if (result.rootPath.isEmpty()) {
        result.error = QStringLiteral("empty storage root");
        return result;
    }
    if (result.devicePath.isEmpty()) {
        result.error = QStringLiteral("empty device path");
        return result;
    }

    QString error;
    if (!ensureDir(result.rootPath, &error)) {
        result.error = error;
        return result;
    }
    if (!ensureDir(QDir(result.rootPath).absoluteFilePath(QStringLiteral("sessions")), &error) ||
        !ensureDir(QDir(result.rootPath).absoluteFilePath(QStringLiteral("latest")), &error)) {
        result.error = error;
        return result;
    }

    result.sessionId = uniqueSessionId(result.rootPath);
    result.sessionPath = QDir(result.rootPath).absoluteFilePath(QStringLiteral("sessions/") + result.sessionId);
    result.framesPath = QDir(result.sessionPath).absoluteFilePath(QStringLiteral("frames"));
    result.videosPath = QDir(result.sessionPath).absoluteFilePath(QStringLiteral("videos"));
    result.latestPath = QDir(result.rootPath).absoluteFilePath(QStringLiteral("latest/current.jpg"));

    if (!ensureDir(result.sessionPath, &error) ||
        !ensureDir(result.framesPath, &error) ||
        !ensureDir(result.videosPath, &error)) {
        result.error = error;
        return result;
    }

    const QString now = timestampText();
    const QString sessionJson = QStringLiteral(
        "{\"session_id\":%1,\"start_time\":%2,\"storage_version\":1,"
        "\"device\":%3,\"mode\":%4}\n")
        .arg(jsonString(result.sessionId), jsonString(now), jsonString(result.devicePath), modeJson(mode));

    if (!writeFile(QDir(result.sessionPath).absoluteFilePath(QStringLiteral("session.json")), sessionJson, &error)) {
        result.error = QStringLiteral("session.json: %1").arg(error);
        return result;
    }

    if (!writeFile(QDir(result.sessionPath).absoluteFilePath(QStringLiteral("events.jsonl")), QString(), &error)) {
        result.error = QStringLiteral("events.jsonl: %1").arg(error);
        return result;
    }

    if (!writeFile(QDir(result.sessionPath).absoluteFilePath(QStringLiteral("index.jsonl")), QString(), &error)) {
        result.error = QStringLiteral("index.jsonl: %1").arg(error);
        return result;
    }

    if (!writeFile(QDir(result.rootPath).absoluteFilePath(QStringLiteral("latest/status.json")),
                   QStringLiteral("{\"ts\":%1,\"session_id\":%2,\"status\":\"session_open\",\"path\":%3}\n")
                       .arg(jsonString(now), jsonString(result.sessionId), jsonString(QStringLiteral("sessions/") + result.sessionId)),
                   &error)) {
        result.error = QStringLiteral("latest/status.json: %1").arg(error);
        return result;
    }

    result.ok = true;
    return result;
}

CameraTestPathResult StorageManager::makeCameraSnapshotPath(const CameraTestSessionResult &session) const
{
    return makeUniquePath(session.framesPath,
                          QStringLiteral("snapshot-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")),
                          QStringLiteral(".jpg"));
}

CameraTestPathResult StorageManager::makeCameraRecordingPath(const CameraTestSessionResult &session) const
{
    return makeUniquePath(session.videosPath,
                          QStringLiteral("recording-") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")),
                          QStringLiteral(".mjpeg"));
}

bool StorageManager::appendCameraEvent(const CameraTestSessionResult &session, const QString &type,
                                       const QString &relativePath, const QString &status,
                                       QString *error) const
{
    if (!session.ok) {
        if (error)
            *error = QStringLiteral("session is not open");
        return false;
    }

    const QString now = timestampText();
    const QString line = QStringLiteral("{\"ts\":%1,\"type\":%2,\"path\":%3,\"status\":%4}\n")
        .arg(jsonString(now), jsonString(type), jsonString(relativePath), jsonString(status));
    return appendFile(QDir(session.sessionPath).absoluteFilePath(QStringLiteral("events.jsonl")), line, error);
}

bool StorageManager::appendCameraIndex(const CameraTestSessionResult &session, int sequence,
                                       const QString &relativePath, const QString &kind,
                                       const QString &status, QString *error) const
{
    if (!session.ok) {
        if (error)
            *error = QStringLiteral("session is not open");
        return false;
    }

    const QString now = timestampText();
    const QString line = QStringLiteral("{\"seq\":%1,\"ts\":%2,\"path\":%3,\"kind\":%4,\"status\":%5}\n")
        .arg(sequence)
        .arg(jsonString(now))
        .arg(jsonString(relativePath))
        .arg(jsonString(kind))
        .arg(jsonString(status));
    return appendFile(QDir(session.sessionPath).absoluteFilePath(QStringLiteral("index.jsonl")), line, error);
}

bool StorageManager::writeLatestStatus(const QString &rootPath, const QString &status,
                                      const QString &relativePath, QString *error) const
{
    const QString latestDir = QDir(rootPath).absoluteFilePath(QStringLiteral("latest"));
    if (!ensureDir(latestDir, error))
        return false;

    const QString now = timestampText();
    const QString line = QStringLiteral("{\"ts\":%1,\"storage\":%2,\"path\":%3}\n")
        .arg(jsonString(now), jsonString(status), jsonString(relativePath));
    return writeFile(QDir(rootPath).absoluteFilePath(QStringLiteral("latest/status.json")), line, error);
}

bool StorageManager::updateLatestImage(const CameraTestSessionResult &session, const QString &sourcePath,
                                      const QString &status, QString *error) const
{
    if (!session.ok) {
        if (error)
            *error = QStringLiteral("session is not open");
        return false;
    }

    const QString latestImagePath = QDir(session.rootPath).absoluteFilePath(QStringLiteral("latest/current.jpg"));
    if (QFile::exists(latestImagePath) && !QFile::remove(latestImagePath)) {
        if (error)
            *error = QStringLiteral("cannot replace latest/current.jpg");
        return false;
    }

    if (!QFile::copy(sourcePath, latestImagePath)) {
        if (error)
            *error = QStringLiteral("cannot copy %1 to latest/current.jpg").arg(sourcePath);
        return false;
    }

    const QString relativePath = sessionRelativePath(session, sourcePath);
    return writeLatestStatus(session.rootPath, status, relativePath, error);
}

} // namespace imx6sm

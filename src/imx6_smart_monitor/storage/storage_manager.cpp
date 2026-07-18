#include "storage/storage_manager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace imx6sm {

static bool ensureDir(const QString &path, QString *error)
{
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty path");
        return false;
    }
    if (QDir().mkpath(path))
        return true;
    if (error)
        *error = QStringLiteral("cannot create %1").arg(path);
    return false;
}

static bool checkWritable(const QString &rootPath, QString *error)
{
    const QString name = QStringLiteral(".storage-write-test-") +
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString path = QDir(rootPath).absoluteFilePath(name);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write("ok\n") < 0) {
        if (error)
            *error = file.errorString().isEmpty() ? QStringLiteral("write failed") : file.errorString();
        file.close();
        QFile::remove(path);
        return false;
    }
    file.close();

    if (!QFile::remove(path)) {
        if (error)
            *error = QStringLiteral("cannot remove write test file");
        return false;
    }
    return true;
}

static QString timestampBase(const QString &prefix)
{
    const QString cleanPrefix = prefix.trimmed().isEmpty() ? QStringLiteral("capture") : prefix.trimmed();
    return cleanPrefix + QLatin1Char('-') +
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
}

static StoragePathResult makeUniquePath(const QString &rootPath, const QString &dirPath,
                                        const QString &prefix, const QString &suffix)
{
    StoragePathResult result;
    QString error;
    if (!ensureDir(dirPath, &error)) {
        result.error = error;
        return result;
    }

    const QString baseName = timestampBase(prefix);
    for (int i = 0; i < 1000; ++i) {
        const QString fileName = i == 0
            ? baseName + suffix
            : baseName + QStringLiteral("-%1").arg(i + 1) + suffix;
        const QString path = QDir(dirPath).absoluteFilePath(fileName);
        if (!QFileInfo::exists(path)) {
            result.ok = true;
            result.path = path;
            result.relativePath = QDir(rootPath).relativeFilePath(path);
            return result;
        }
    }

    result.error = QStringLiteral("cannot allocate unique path in %1").arg(dirPath);
    return result;
}

StorageCheckResult StorageManager::checkRoot(const QString &rootPath) const
{
    StorageCheckResult result;
    result.rootPath = QDir(rootPath.trimmed()).absolutePath();

    if (rootPath.trimmed().isEmpty()) {
        result.rootPath.clear();
        result.error = QStringLiteral("empty storage root");
        return result;
    }

    result.framesPath = QDir(result.rootPath).absoluteFilePath(QStringLiteral("frames"));
    result.videosPath = QDir(result.rootPath).absoluteFilePath(QStringLiteral("videos"));

    QString error;
    if (!ensureDir(result.rootPath, &error) ||
        !ensureDir(result.framesPath, &error) ||
        !ensureDir(result.videosPath, &error) ||
        !checkWritable(result.rootPath, &error)) {
        result.error = error;
        return result;
    }

    result.ok = true;
    return result;
}

StoragePathResult StorageManager::makeFramePath(const QString &rootPath, const QString &prefix) const
{
    const StorageCheckResult root = checkRoot(rootPath);
    if (!root.ok) {
        StoragePathResult result;
        result.error = root.error;
        return result;
    }
    return makeUniquePath(root.rootPath, root.framesPath, prefix, QStringLiteral(".jpg"));
}

StoragePathResult StorageManager::makeVideoPath(const QString &rootPath, const QString &prefix) const
{
    const StorageCheckResult root = checkRoot(rootPath);
    if (!root.ok) {
        StoragePathResult result;
        result.error = root.error;
        return result;
    }
    return makeUniquePath(root.rootPath, root.videosPath, prefix, QStringLiteral(".mjpeg"));
}

} // namespace imx6sm

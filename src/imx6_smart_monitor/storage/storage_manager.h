#ifndef IMX6SMARTMONITOR_STORAGE_MANAGER_H
#define IMX6SMARTMONITOR_STORAGE_MANAGER_H

#include "imx6smartmonitor/types.h"

#include <QString>

namespace imx6sm {

struct StorageCheckResult {
    bool ok = false;
    QString rootPath;
    QString sessionPath;
    QString error;
};

struct CameraTestSessionResult {
    bool ok = false;
    QString rootPath;
    QString sessionId;
    QString sessionPath;
    QString framesPath;
    QString videosPath;
    QString latestPath;
    QString devicePath;
    CameraMode mode;
    QString error;
};

struct CameraTestPathResult {
    bool ok = false;
    QString path;
    QString relativePath;
    QString error;
};

class StorageManager {
public:
    StorageCheckResult checkRoot(const QString &rootPath) const;
    CameraTestSessionResult openCameraTestSession(const QString &rootPath,
                                                  const QString &devicePath,
                                                  const CameraMode &mode) const;
    CameraTestPathResult makeCameraSnapshotPath(const CameraTestSessionResult &session) const;
    CameraTestPathResult makeCameraRecordingPath(const CameraTestSessionResult &session) const;
    bool appendCameraEvent(const CameraTestSessionResult &session, const QString &type,
                           const QString &relativePath, const QString &status,
                           QString *error) const;
    bool appendCameraIndex(const CameraTestSessionResult &session, int sequence,
                           const QString &relativePath, const QString &kind,
                           const QString &status, QString *error) const;
    bool writeLatestStatus(const QString &rootPath, const QString &status,
                           const QString &relativePath, QString *error) const;
    bool updateLatestImage(const CameraTestSessionResult &session, const QString &sourcePath,
                           const QString &status, QString *error) const;
};

} // namespace imx6sm

#endif

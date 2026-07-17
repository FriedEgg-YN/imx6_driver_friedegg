#ifndef IMX6SMARTMONITOR_STORAGE_MANAGER_H
#define IMX6SMARTMONITOR_STORAGE_MANAGER_H

#include "imx6smartmonitor/types.h"

#include <QList>
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

struct MonitorSessionResult {
    bool ok = false;
    QString rootPath;
    QString sessionId;
    QString sessionPath;
    QString framesPath;
    QString latestPath;
    QString devicePath;
    CameraMode mode;
    QString error;
};

struct MonitorSessionInfo {
    QString sessionId;
    QString sessionPath;
    QString startTime;
    QString latestImagePath;
    int eventCount = 0;
    int frameCount = 0;
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

    MonitorSessionResult openMonitorSession(const QString &rootPath,
                                            const QString &devicePath,
                                            const CameraMode &mode) const;
    CameraTestPathResult makeMonitorSnapshotPath(const MonitorSessionResult &session) const;
    bool appendMonitorEvent(const MonitorSessionResult &session, const QString &type,
                            const QString &status, const QString &detail,
                            QString *error) const;
    bool appendMonitorIndex(const MonitorSessionResult &session, int sequence,
                            const QString &relativePath, const QString &kind,
                            const QString &status, QString *error) const;
    bool updateLatestImage(const MonitorSessionResult &session, const QString &sourcePath,
                           const QString &status, QString *error) const;
    bool closeMonitorSession(const MonitorSessionResult &session, const QString &status,
                             QString *error) const;
    QList<MonitorSessionInfo> listMonitorSessions(const QString &rootPath,
                                                  QString *error) const;
};

} // namespace imx6sm

Q_DECLARE_METATYPE(imx6sm::MonitorSessionInfo)
Q_DECLARE_METATYPE(QList<imx6sm::MonitorSessionInfo>)

#endif

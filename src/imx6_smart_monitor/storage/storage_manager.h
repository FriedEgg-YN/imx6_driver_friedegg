#ifndef IMX6SMARTMONITOR_STORAGE_MANAGER_H
#define IMX6SMARTMONITOR_STORAGE_MANAGER_H

#include "imx6smartmonitor/types.h"

#include <QString>

namespace imx6sm {

struct StorageCheckResult {
    bool ok = false;
    QString rootPath;
    QString framesPath;
    QString videosPath;
    QString error;
};

struct StoragePathResult {
    bool ok = false;
    QString path;
    QString relativePath;
    QString error;
};

/*
 * StorageManager 只负责 Smart Monitor 的文件落点：
 *   /smart-monitor/frames/<timestamp>.jpg
 *   /smart-monitor/videos/<timestamp>.mjpeg
 *
 * 它不维护 session、latest、json/jsonl 索引，也不保存业务状态。调用方只需要
 * checkRoot() 确认目录可写，然后用 makeFramePath()/makeVideoPath() 得到唯一路径。
 */
class StorageManager {
public:
    StorageCheckResult checkRoot(const QString &rootPath) const;
    StoragePathResult makeFramePath(const QString &rootPath,
                                    const QString &prefix = QStringLiteral("frame")) const;
    StoragePathResult makeVideoPath(const QString &rootPath,
                                    const QString &prefix = QStringLiteral("recording")) const;
};

} // namespace imx6sm

#endif

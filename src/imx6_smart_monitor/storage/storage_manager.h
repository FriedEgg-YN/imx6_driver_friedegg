#ifndef IMX6SMARTMONITOR_STORAGE_MANAGER_H
#define IMX6SMARTMONITOR_STORAGE_MANAGER_H

#include <QString>

namespace imx6sm {

struct StorageCheckResult {
    bool ok = false;
    QString rootPath;
    QString sessionPath;
    QString error;
};

class StorageManager {
public:
    StorageCheckResult checkRoot(const QString &rootPath) const;
};

} // namespace imx6sm

#endif


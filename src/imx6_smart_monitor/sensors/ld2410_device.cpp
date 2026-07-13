#include "sensors/ld2410_device.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace imx6sm {

QString Ld2410Device::findInputHint() const
{
    QFile file(QStringLiteral("/proc/bus/input/devices"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QString currentName;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.startsWith(QStringLiteral("N:"))) {
            currentName = line;
            continue;
        }

        if (!line.startsWith(QStringLiteral("H:")))
            continue;

        const QString lower = (currentName + QLatin1Char(' ') + line).toLower();
        if (!lower.contains(QStringLiteral("ld2410")) && !lower.contains(QStringLiteral("presence")))
            continue;

        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (part.startsWith(QStringLiteral("event")))
                return QStringLiteral("/dev/input/") + part;
        }
    }

    return QString();
}

Ld2410Probe Ld2410Device::probe(const QString &outPath, const QString &uartPath) const
{
    Ld2410Probe result;
    result.inputHint = findInputHint();
    result.outPath = outPath == QStringLiteral("auto") ? result.inputHint : outPath;
    result.uartPath = uartPath;

    if (!result.outPath.isEmpty())
        result.outAvailable = QFileInfo::exists(result.outPath);

    if (!result.uartPath.isEmpty())
        result.uartAvailable = QFileInfo::exists(result.uartPath);

    if (!result.outAvailable && !result.uartAvailable)
        result.error = QStringLiteral("no LD2410C OUT/input or UART node found");

    return result;
}

} // namespace imx6sm


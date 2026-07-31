#include "cadmouseconfig.h"

#include <QFile>
#include <QTextStream>

QString CadMouseConfig::resolveConfigPath(const QString &explicitPath,
                                          const QString &applicationDir) {
    if (!explicitPath.isEmpty() && QFile::exists(explicitPath))
        return explicitPath;

    // 与 cadmouse-gui / cadmouse-helper 同目录（开发 build/ 与安装 files/ 皆适用）
    const QString beside = applicationDir + QStringLiteral("/cadmouse.conf");
    if (QFile::exists(beside))
        return beside;

    return explicitPath.isEmpty() ? beside : explicitPath;
}

bool CadMouseConfig::load(const QString &filePath, CadMouseServiceConfig *out) {
    if (!out)
        return false;

    // 合理默认：与旧脚本 VAL_SPEED=1c(档4)、1000Hz、重映射开 一致
    out->devicePath = QStringLiteral("/dev/cadmouse_config");
    out->heartbeatIntervalSec = 2.0;
    out->heartbeatStartupDelaySec = 0.5;
    out->bulk.speedIndex = 4;
    out->bulk.pollHz = 1000;
    out->bulk.remapWheel = true;
    out->bulk.remapGesture = true;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;

        const QString key = line.left(eq).trimmed().toLower();
        const QString value = line.mid(eq + 1).trimmed();

        if (key == QLatin1String("device_path") || key == QLatin1String("dev_path")) {
            out->devicePath = value;
        } else if (key == QLatin1String("heartbeat_interval_sec")) {
            bool ok = false;
            const double v = value.toDouble(&ok);
            if (ok && v > 0.05)
                out->heartbeatIntervalSec = v;
        } else if (key == QLatin1String("heartbeat_startup_delay_sec")) {
            bool ok = false;
            const double v = value.toDouble(&ok);
            if (ok && v >= 0.0)
                out->heartbeatStartupDelaySec = v;
        } else if (key == QLatin1String("speed")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok && v >= 1 && v <= 5)
                out->bulk.speedIndex = v;
        } else if (key == QLatin1String("speed_byte") || key == QLatin1String("val_speed")) {
            bool ok = false;
            QString hex = value;
            if (hex.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
                hex = hex.mid(2);
            const int b = hex.toInt(&ok, 16);
            if (ok) {
                const auto map = CadMouseProtocol::speedIndexFromByte();
                if (map.contains(static_cast<quint8>(b)))
                    out->bulk.speedIndex = map.value(static_cast<quint8>(b));
            }
        } else if (key == QLatin1String("polling_hz") || key == QLatin1String("poll_hz")) {
            bool ok = false;
            const int v = value.toInt(&ok);
            if (ok && CadMouseProtocol::pollByteMap().contains(v))
                out->bulk.pollHz = v;
        } else if (key == QLatin1String("val_polling")) {
            bool ok = false;
            QString hex = value;
            if (hex.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
                hex = hex.mid(2);
            const int b = hex.toInt(&ok, 16);
            if (ok) {
                const auto map = CadMouseProtocol::pollHzFromByte();
                if (map.contains(static_cast<quint8>(b)))
                    out->bulk.pollHz = map.value(static_cast<quint8>(b));
            }
        } else if (key == QLatin1String("remap_wheel")) {
            out->bulk.remapWheel = !(value == QLatin1String("0")
                                     || value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0
                                     || value.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0);
        } else if (key == QLatin1String("remap_gesture")) {
            out->bulk.remapGesture = !(value == QLatin1String("0")
                                       || value.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0
                                       || value.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0);
        }
    }
    return true;
}

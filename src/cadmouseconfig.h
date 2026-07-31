#ifndef CADMOUSECONFIG_H
#define CADMOUSECONFIG_H

#include "cadmouseprotocol.h"

#include <QString>

/**
 * 从 KEY=VALUE 文本配置加载默认参数（心跳服务与初始化共用）。
 */
struct CadMouseServiceConfig {
    QString devicePath = QStringLiteral("/dev/cadmouse_config");
    double heartbeatIntervalSec = 2.0;
    double heartbeatStartupDelaySec = 0.5;
    CadMouseProtocol::BulkConfig bulk; // 默认 speed=4, poll=1000, remaps on
};

class CadMouseConfig {
public:
    /** 按路径加载；失败时 out 保持默认值并返回 false */
    static bool load(const QString &filePath, CadMouseServiceConfig *out);

    /**
     * 解析配置路径：显式 --config > 可执行文件同目录 cadmouse.conf。
     * 不硬编码安装前缀；打包时 conf 与二进制放在同一 files/ 目录即可。
     */
    static QString resolveConfigPath(const QString &explicitPath,
                                     const QString &applicationDir);
};

#endif // CADMOUSECONFIG_H

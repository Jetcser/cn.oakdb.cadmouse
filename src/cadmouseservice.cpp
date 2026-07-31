#include "cadmouseservice.h"

#include "cadmousehid.h"
#include "cadmouseprotocol.h"

#include <QFile>
#include <QStringList>
#include <QThread>
#include <cstdio>

namespace CadMouseService {
namespace {

void sleepSec(double seconds) {
    if (seconds <= 0)
        return;
    QThread::msleep(static_cast<unsigned long>(seconds * 1000.0));
}

bool sendInitConfig(CadMouseHid &hid, const CadMouseServiceConfig &cfg) {
    const QByteArray packet = CadMouseProtocol::buildBulkPacket(cfg.bulk);
    if (hid.sendFeature(packet))
        return true;

    std::fprintf(stderr, "send-feature 失败，尝试 send-output: %s\n",
                 qPrintable(hid.lastError()));
    return hid.sendOutput(packet);
}

} // namespace

int runHeartbeat(const CadMouseServiceConfig &cfg) {
    sleepSec(cfg.heartbeatStartupDelaySec);

    if (!QFile::exists(cfg.devicePath)) {
        std::fprintf(stderr, "错误: 未找到设备节点 %s\n", qPrintable(cfg.devicePath));
        return 1;
    }

    {
        CadMouseHid hid;
        if (!hid.openPath(cfg.devicePath)) {
            std::fprintf(stderr, "打开设备失败: %s\n", qPrintable(hid.lastError()));
            return 1;
        }
        std::fprintf(stdout, "正在发送 Feature 初始化配置 (speed=%d, poll=%dHz)...\n",
                     cfg.bulk.speedIndex, cfg.bulk.pollHz);
        if (sendInitConfig(hid, cfg))
            std::fprintf(stdout, "初始化成功。\n");
        else
            std::fprintf(stderr, "初始化失败: %s\n", qPrintable(hid.lastError()));
    }

    std::fprintf(stdout, "进入心跳维持模式 (间隔 %.2f 秒)...\n", cfg.heartbeatIntervalSec);
    const QByteArray heartbeat = QByteArray(1, char(0x00));

    while (QFile::exists(cfg.devicePath)) {
        CadMouseHid hid;
        if (hid.openPath(cfg.devicePath)) {
            if (!hid.sendOutput(heartbeat))
                std::fprintf(stderr, "心跳失败: %s\n", qPrintable(hid.lastError()));
        }
        sleepSec(cfg.heartbeatIntervalSec);
    }

    std::fprintf(stdout, "设备已移除，退出心跳。\n");
    return 0;
}

int runInitReceiver(const CadMouseServiceConfig &cfg) {
    sleepSec(1.0);

    QStringList targets = CadMouseHid::listManagementPaths(0xc652);
    if (targets.isEmpty() && QFile::exists(cfg.devicePath))
        targets << cfg.devicePath;

    if (targets.isEmpty()) {
        std::fprintf(stderr, "未找到 C652 管理接口或 %s\n", qPrintable(cfg.devicePath));
        return 1;
    }

    const QByteArray packet = CadMouseProtocol::buildBulkPacket(cfg.bulk);
    int okCount = 0;
    for (const QString &path : targets) {
        std::fprintf(stdout, "正在配置设备 %s (speed=%d, poll=%dHz)...\n",
                     qPrintable(path), cfg.bulk.speedIndex, cfg.bulk.pollHz);
        CadMouseHid hid;
        if (!hid.openPath(path)) {
            std::fprintf(stderr, "  打开失败: %s\n", qPrintable(hid.lastError()));
            continue;
        }
        if (hid.sendFeature(packet))
            ++okCount;
        else
            std::fprintf(stderr, "  发送失败: %s\n", qPrintable(hid.lastError()));
    }
    return okCount > 0 ? 0 : 1;
}

} // namespace CadMouseService

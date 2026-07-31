/**
 * cadmouse-helper — 无界面后台工具（仅 Qt Core + libhidapi）
 *
 *   cadmouse-helper --heartbeat [--config PATH]
 *   cadmouse-helper --init-receiver [--config PATH]
 */
#include "cadmouseconfig.h"
#include "cadmouseservice.h"

#include <QCoreApplication>
#include <cstdio>

namespace {

void printUsage() {
    std::fprintf(stderr,
                 "Usage:\n"
                 "  cadmouse-helper --heartbeat [--config PATH]\n"
                 "  cadmouse-helper --init-receiver [--config PATH]\n");
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cadmouse-helper"));

    const QStringList args = app.arguments();
    QString configArg;
    bool doHeartbeat = false;
    bool doInitReceiver = false;

    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QLatin1String("--heartbeat")) {
            doHeartbeat = true;
        } else if (a == QLatin1String("--init-receiver")) {
            doInitReceiver = true;
        } else if (a == QLatin1String("--config") && i + 1 < args.size()) {
            configArg = args.at(++i);
        } else if (a == QLatin1String("-h") || a == QLatin1String("--help")) {
            printUsage();
            return 0;
        } else {
            std::fprintf(stderr, "未知参数: %s\n", qPrintable(a));
            printUsage();
            return 2;
        }
    }

    if (doHeartbeat == doInitReceiver) {
        printUsage();
        return 2;
    }

    const QString confPath = CadMouseConfig::resolveConfigPath(
        configArg, QCoreApplication::applicationDirPath());

    CadMouseServiceConfig cfg;
    if (CadMouseConfig::load(confPath, &cfg))
        std::fprintf(stdout, "已加载配置: %s\n", qPrintable(confPath));
    else
        std::fprintf(stderr, "警告: 无法读取配置 %s，使用内置默认值\n",
                     qPrintable(confPath));

    if (doHeartbeat)
        return CadMouseService::runHeartbeat(cfg);
    return CadMouseService::runInitReceiver(cfg);
}

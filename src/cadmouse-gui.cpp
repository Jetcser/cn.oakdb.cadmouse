/**
 * cadmouse-gui — 3Dconnexion CadMouse 配置 GUI
 * Copyright © 2019 František Kučera (Frantovo.cz, GlobalCode.info)
 *
 * 后台心跳/接收器初始化见同目录 cadmouse-helper。
 */
#include "mousemainwindow.h"

#include <QApplication>

int main(int argc, char **argv) {
    QApplication qtApplication(argc, argv);
    MouseMainWindow window;
    window.setWindowTitle(QStringLiteral("3Dconnexion CadMouse Utilty"));
    window.show();
    return qtApplication.exec();
}

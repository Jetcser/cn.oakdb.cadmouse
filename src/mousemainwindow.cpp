#include "mousemainwindow.h"
#include "cadmousehid.h"
#include "cadmouseprotocol.h"
#include "ui_mousemainwindow.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPixmap>
#include <QRegularExpression>
#include <QSize>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent>

MouseMainWindow::MouseMainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MouseMainWindow) {
    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);

    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    setFixedSize(sizeHint().expandedTo(QSize(400, 420)));

    // 结果框仅展示，不参与键盘/鼠标交互
    ui->lineEditResult->setFocusPolicy(Qt::NoFocus);
    ui->lineEditResult->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    ui->lineEditResult->setReadOnly(true);

    statusClearTimer = new QTimer(this);
    statusClearTimer->setSingleShot(true);
    connect(statusClearTimer, &QTimer::timeout, this, [this]() {
        ui->lineEditResult->clear();
        ui->lineEditResult->setStyleSheet(QString());
    });

    setupDeviceWatcher();

    findMouseDevice();
    initParameter();
    refreshStatus();
    if (!hidrawDevicePath.isEmpty())
        startReadConfigAsync();
}

MouseMainWindow::~MouseMainWindow() {
    delete ui;
}

void MouseMainWindow::setupDeviceWatcher() {
    devWatcher = new QFileSystemWatcher(this);
    if (QDir(QStringLiteral("/dev")).exists())
        devWatcher->addPath(QStringLiteral("/dev"));

    devDebounceTimer = new QTimer(this);
    devDebounceTimer->setSingleShot(true);
    devDebounceTimer->setInterval(400);
    connect(devDebounceTimer, &QTimer::timeout, this, [this]() {
        const QString before = hidrawDevicePath;
        findMouseDevice();
        refreshStatus();
        if (!hidrawDevicePath.isEmpty() && hidrawDevicePath != before)
            startReadConfigAsync();
    });

    connect(devWatcher, &QFileSystemWatcher::directoryChanged,
            this, &MouseMainWindow::onDevicePathChanged);
    connect(devWatcher, &QFileSystemWatcher::fileChanged,
            this, &MouseMainWindow::onDevicePathChanged);
}

void MouseMainWindow::onDevicePathChanged() {
    if (!devDebounceTimer->isActive())
        devDebounceTimer->start();
}

void MouseMainWindow::findMouseDevice() {
    const QString linkPath = QStringLiteral("/dev/cadmouse_config");
    if (!QFile::exists(linkPath)) {
        hidrawDevicePath.clear();
        hidDevicePath.clear();
        bulkConfigReadable = true;
        qDebug() << "未发现 /dev/cadmouse_config，请检查 udev 规则和权限";
        return;
    }

    const QString previousHid = hidDevicePath;
    hidrawDevicePath = linkPath;

    QString target = QFile::symLinkTarget(linkPath);
    if (target.isEmpty())
        target = QFileInfo(linkPath).canonicalFilePath();
    if (!target.startsWith(QLatin1Char('/')))
        target = QStringLiteral("/dev/") + target;

    static const QRegularExpression hidrawRe(QStringLiteral("hidraw(\\d+)"));
    const auto match = hidrawRe.match(target);
    if (match.hasMatch()) {
        const QString node = QStringLiteral("hidraw") + match.captured(1);
        hidDevicePath = QStringLiteral("/sys/class/hidraw/") + node + QStringLiteral("/device");
        if (previousHid != hidDevicePath)
            bulkConfigReadable = true;
        qDebug() << "设备已锁定:" << hidrawDevicePath << "->" << node;
    } else {
        hidDevicePath.clear();
        qDebug() << "无法解析 hidraw 节点:" << target;
    }
}

void MouseMainWindow::refreshStatus() {
    const bool isConnected = !hidrawDevicePath.isEmpty();
    currentPID = QStringLiteral("EMPTY");
    QString deviceName = QStringLiteral("未找到设备");

    if (isConnected && !hidDevicePath.isEmpty()) {
        const QString ueventPath = hidDevicePath + QStringLiteral("/uevent");
        const QString hidId = readUeventValue(ueventPath, QStringLiteral("HID_ID"));
        static const QRegularExpression pidRe(
            QStringLiteral("256[Ff]:0*([0-9A-Fa-f]{4})"), QRegularExpression::CaseInsensitiveOption);
        const auto pidMatch = pidRe.match(hidId);
        if (pidMatch.hasMatch())
            currentPID = pidMatch.captured(1).toUpper();

        QString hidName = readUeventValue(ueventPath, QStringLiteral("HID_NAME"));
        if (hidName.isEmpty()) {
            QDir dir(hidDevicePath);
            for (int i = 0; i < 6; ++i) {
                const QString product = readFileContent(dir.absoluteFilePath(QStringLiteral("product")));
                if (!product.isEmpty()) {
                    hidName = product;
                    break;
                }
                if (!dir.cdUp())
                    break;
            }
        }
        if (!hidName.isEmpty()) {
            deviceName = hidName;
            if (deviceName.startsWith(QStringLiteral("3Dconnexion "), Qt::CaseInsensitive))
                deviceName = deviceName.mid(QStringLiteral("3Dconnexion ").size());
        } else {
            deviceName = QStringLiteral("CadMouse");
        }
    }

    ui->labelDeviceName->setText(deviceName);

    QString imagePath = QStringLiteral(":/devices/images/%1.png").arg(currentPID);
    if (!QFile::exists(imagePath))
        imagePath = QStringLiteral(":/devices/images/empty.png");

    const QPixmap pixmap(imagePath);
    if (pixmap.isNull()) {
        ui->deviceImage->clear();
    } else {
        // 资源图为 200x120，原尺寸显示，不做缩放
        ui->deviceImage->setPixmap(pixmap);
    }

    const int batteryLevel = getBatteryLevelFromsysfs();
    const bool isBatteryValid = (batteryLevel >= 0 && batteryLevel <= 100);

    ui->labelBattery->setVisible(isBatteryValid);
    ui->progressBarBatteryLevel->setVisible(isBatteryValid);
    ui->labelBatteryLevel->setVisible(isBatteryValid);

    if (isBatteryValid) {
        ui->progressBarBatteryLevel->setValue(batteryLevel);
        ui->labelBatteryLevel->setText(QString::number(batteryLevel) + QLatin1Char('%'));
    }
}

void MouseMainWindow::applyBulkConfigToUi(const QByteArray &packet) {
    CadMouseProtocol::BulkConfig cfg;
    if (!CadMouseProtocol::parseBulkPacket(packet, &cfg))
        return;

    switch (cfg.speedIndex) {
    case 1: ui->radioButtonSpeed1->setChecked(true); break;
    case 2: ui->radioButtonSpeed2->setChecked(true); break;
    case 4: ui->radioButtonSpeed4->setChecked(true); break;
    case 5: ui->radioButtonSpeed5->setChecked(true); break;
    default: ui->radioButtonSpeed3->setChecked(true); break;
    }

    switch (cfg.pollHz) {
    case 125: ui->radioButtonFrequency125->setChecked(true); break;
    case 250: ui->radioButtonFrequency250->setChecked(true); break;
    case 500: ui->radioButtonFrequency500->setChecked(true); break;
    default: ui->radioButtonFrequency1000->setChecked(true); break;
    }

    ui->checkBoxRemapWheelPressed->setChecked(cfg.remapWheel);
    ui->checkBoxRemapGestureButton->setChecked(cfg.remapGesture);
}

QString MouseMainWindow::classifyHidFailure(const QString &hidError) const {
    const QString combined = hidError.toLower();
    if (combined.contains(QStringLiteral("permission")) || combined.contains(QStringLiteral("eacces"))
        || combined.contains(QStringLiteral("denied")))
        return QStringLiteral("同步失败：无权限访问设备");
    if (combined.contains(QStringLiteral("busy")) || combined.contains(QStringLiteral("ebusy")))
        return QStringLiteral("同步失败：设备忙");
    if (combined.contains(QStringLiteral("no such")) || combined.contains(QStringLiteral("enoent"))
        || !QFile::exists(hidrawDevicePath))
        return QStringLiteral("同步失败：设备已断开");
    if (!hidError.isEmpty())
        return QStringLiteral("同步失败：%1").arg(hidError);
    return QStringLiteral("同步失败：未知错误");
}

void MouseMainWindow::startReadConfigAsync() {
    if (hidrawDevicePath.isEmpty() || !bulkConfigReadable)
        return;
    bool expected = false;
    if (!hidBusy.compare_exchange_strong(expected, true))
        return;

    const QString path = hidrawDevicePath;
    QtConcurrent::run([this, path]() {
        CadMouseHid hid;
        QByteArray packet;
        QString err;
        if (!hid.openPath(path)) {
            err = hid.lastError();
        } else {
            // 部分设备对 GET_FEATURE 要求缓冲 ≥ 描述符长度；用 256 兼容性更好
            packet = hid.readFeature(CadMouseProtocol::kBulkReportId, 256);
            if (packet.isEmpty())
                err = hid.lastError();
            else if (packet.size() > CadMouseProtocol::kBulkReportLen)
                packet.resize(CadMouseProtocol::kBulkReportLen);
        }

        QTimer::singleShot(0, this, [this, packet, err]() {
            hidBusy = false;
            if (!packet.isEmpty()) {
                applyBulkConfigToUi(packet);
                return;
            }
            if (err.isEmpty())
                return;

            const QString lower = err.toLower();
            const bool unsupported = lower.contains(QLatin1String("pipe"))
                || err.contains(QString::fromUtf8("管道"))
                || lower.contains(QLatin1String("epipe"));
            if (unsupported) {
                bulkConfigReadable = false;
                qDebug() << "当前设备不支持配置回读（Feature 0x10 GET），界面保留默认/写入值";
            } else {
                qDebug() << "读取配置失败:" << err;
            }
        });
    });
}

void MouseMainWindow::startConfigureAsync() {
    if (hidrawDevicePath.isEmpty()) {
        showStatus(QStringLiteral("错误: 设备未连接"), false);
        return;
    }

    bool expected = false;
    if (!hidBusy.compare_exchange_strong(expected, true)) {
        showStatus(QStringLiteral("请等待当前操作完成"), false);
        return;
    }

    CadMouseProtocol::BulkConfig cfg;
    cfg.speedIndex = 3;
    if (ui->radioButtonSpeed1->isChecked()) cfg.speedIndex = 1;
    else if (ui->radioButtonSpeed2->isChecked()) cfg.speedIndex = 2;
    else if (ui->radioButtonSpeed4->isChecked()) cfg.speedIndex = 4;
    else if (ui->radioButtonSpeed5->isChecked()) cfg.speedIndex = 5;

    cfg.pollHz = 1000;
    if (ui->radioButtonFrequency500->isChecked()) cfg.pollHz = 500;
    else if (ui->radioButtonFrequency250->isChecked()) cfg.pollHz = 250;
    else if (ui->radioButtonFrequency125->isChecked()) cfg.pollHz = 125;

    cfg.remapWheel = ui->checkBoxRemapWheelPressed->isChecked();
    cfg.remapGesture = ui->checkBoxRemapGestureButton->isChecked();

    QList<QByteArray> packets;
    packets << CadMouseProtocol::buildBulkPacket(cfg);
    packets << CadMouseProtocol::buildSmartScrollPackets(ui->checkBoxSmartScrolling->isChecked());
    packets << CadMouseProtocol::buildLiftoffPacket(ui->checkBoxLiftOffDetection->isChecked());

    ui->pushButtonConfigure->setEnabled(false);
    const QString path = hidrawDevicePath;

    QtConcurrent::run([this, path, packets]() {
        CadMouseHid hid;
        QString err;
        bool bulkOk = false;

        if (!hid.openPath(path)) {
            err = hid.lastError();
        } else {
            for (int i = 0; i < packets.size(); ++i) {
                if (!hid.sendFeature(packets.at(i))) {
                    if (i == 0) {
                        err = hid.lastError();
                        break;
                    }
                    qDebug() << "附加命令失败:" << hid.lastError();
                } else if (i == 0) {
                    bulkOk = true;
                }
            }
        }

        QTimer::singleShot(0, this, [this, bulkOk, err]() {
            hidBusy = false;
            ui->pushButtonConfigure->setEnabled(true);
            if (bulkOk)
                showStatus(QStringLiteral("配置已同步"), true);
            else
                showStatus(classifyHidFailure(err), false);
        });
    });
}

void MouseMainWindow::on_pushButtonConfigure_clicked() {
    startConfigureAsync();
}

void MouseMainWindow::showStatus(const QString &msg, bool success) {
    ui->lineEditResult->setText(msg);
    ui->lineEditResult->setStyleSheet(
        success ? QStringLiteral("color: green; font-weight: bold;")
                : QStringLiteral("color: red;"));
    statusClearTimer->start(1500);
}

int MouseMainWindow::getBatteryLevelFromsysfs() const {
    QDir dir(QStringLiteral("/sys/class/power_supply"));
    if (!dir.exists())
        return -1;

    dir.setNameFilters(QStringList() << QStringLiteral("*hid*256F*")
                                     << QStringLiteral("*hid*256f*")
                                     << QStringLiteral("hidpp_battery*"));
    const QStringList batteryDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (batteryDirs.isEmpty())
        return -1;

    QString preferredUsbRoot;
    if (!hidDevicePath.isEmpty()) {
        QDir walk(hidDevicePath);
        for (int i = 0; i < 8; ++i) {
            if (QFile::exists(walk.absoluteFilePath(QStringLiteral("idVendor")))) {
                preferredUsbRoot = walk.canonicalPath();
                break;
            }
            if (!walk.cdUp())
                break;
        }
    }

    auto readCapacity = [this](const QString &batPath) -> int {
        const QString capacity = readFileContent(batPath + QStringLiteral("/capacity"));
        if (capacity.isEmpty())
            return -1;
        bool ok = false;
        const int level = capacity.toInt(&ok);
        return (ok && level >= 0 && level <= 100) ? level : -1;
    };

    if (!preferredUsbRoot.isEmpty()) {
        for (const QString &name : batteryDirs) {
            const QString batPath = dir.absoluteFilePath(name);
            const QString batDev = QFileInfo(batPath + QStringLiteral("/device")).canonicalFilePath();
            if (!batDev.isEmpty()
                && (batDev.startsWith(preferredUsbRoot) || preferredUsbRoot.startsWith(batDev))) {
                const int level = readCapacity(batPath);
                if (level >= 0)
                    return level;
            }
        }
    }

    if (currentPID != QStringLiteral("EMPTY")) {
        for (const QString &name : batteryDirs) {
            if (!name.contains(currentPID, Qt::CaseInsensitive))
                continue;
            const int level = readCapacity(dir.absoluteFilePath(name));
            if (level >= 0)
                return level;
        }
    }

    for (const QString &name : batteryDirs) {
        if (!name.contains(QStringLiteral("256F"), Qt::CaseInsensitive)
            && !name.contains(QStringLiteral("256f")))
            continue;
        const int level = readCapacity(dir.absoluteFilePath(name));
        if (level >= 0)
            return level;
    }

    return -1;
}

void MouseMainWindow::initParameter() {
    ui->radioButtonFrequency1000->setChecked(true);
    ui->radioButtonSpeed3->setChecked(true);
    ui->checkBoxRemapWheelPressed->setChecked(true);
    ui->checkBoxRemapGestureButton->setChecked(true);
    ui->checkBoxSmartScrolling->setChecked(true);
    ui->checkBoxLiftOffDetection->setChecked(true);
}

void MouseMainWindow::on_pushButtonRefresh_clicked() {
    findMouseDevice();
    refreshStatus();
    if (!hidrawDevicePath.isEmpty())
        startReadConfigAsync();
}

QString MouseMainWindow::readFileContent(const QString &filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QTextStream(&file).readAll().trimmed();
}

QString MouseMainWindow::readUeventValue(const QString &ueventPath, const QString &key) const {
    const QString content = readFileContent(ueventPath);
    const QString prefix = key + QLatin1Char('=');
    for (const QString &line : content.split(QLatin1Char('\n'))) {
        if (line.startsWith(prefix))
            return line.mid(prefix.size()).trimmed();
    }
    return QString();
}

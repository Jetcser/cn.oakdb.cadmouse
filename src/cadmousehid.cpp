#include "cadmousehid.h"

#include <QMutex>
#include <QMutexLocker>

namespace {
QMutex g_hidInitMutex;
bool g_hidInited = false;
}

void CadMouseHid::ensureInit() {
    QMutexLocker lock(&g_hidInitMutex);
    if (!g_hidInited) {
        hid_init();
        g_hidInited = true;
    }
}

CadMouseHid::CadMouseHid() {
    ensureInit();
}

CadMouseHid::~CadMouseHid() {
    close();
}

void CadMouseHid::captureError(const char *prefix) {
    const wchar_t *err = hid_error(dev_);
    if (err && err[0] != L'\0') {
        lastError_ = QString::fromWCharArray(err);
        if (prefix && prefix[0])
            lastError_ = QString::fromLatin1(prefix) + QStringLiteral(": ") + lastError_;
    } else if (prefix && prefix[0]) {
        lastError_ = QString::fromLatin1(prefix);
    } else {
        lastError_ = QStringLiteral("unknown HID error");
    }
}

bool CadMouseHid::openPath(const QString &path) {
    close();
    lastError_.clear();
    if (path.isEmpty()) {
        lastError_ = QStringLiteral("empty device path");
        return false;
    }

    dev_ = hid_open_path(path.toUtf8().constData());
    if (!dev_) {
        lastError_ = QStringLiteral("无法打开设备: %1").arg(path);
        return false;
    }
    hid_set_nonblocking(dev_, 0);
    return true;
}

void CadMouseHid::close() {
    if (dev_) {
        hid_close(dev_);
        dev_ = nullptr;
    }
}

bool CadMouseHid::sendFeature(const QByteArray &report) {
    lastError_.clear();
    if (!dev_) {
        lastError_ = QStringLiteral("device not open");
        return false;
    }
    if (report.isEmpty()) {
        lastError_ = QStringLiteral("empty feature report");
        return false;
    }
    const int n = hid_send_feature_report(
        dev_,
        reinterpret_cast<const unsigned char *>(report.constData()),
        static_cast<size_t>(report.size()));
    if (n < 0) {
        captureError("send_feature");
        return false;
    }
    return true;
}

bool CadMouseHid::sendOutput(const QByteArray &report) {
    lastError_.clear();
    if (!dev_) {
        lastError_ = QStringLiteral("device not open");
        return false;
    }
    if (report.isEmpty()) {
        lastError_ = QStringLiteral("empty output report");
        return false;
    }
    const int n = hid_write(
        dev_,
        reinterpret_cast<const unsigned char *>(report.constData()),
        static_cast<size_t>(report.size()));
    if (n < 0) {
        captureError("send_output");
        return false;
    }
    return true;
}

QByteArray CadMouseHid::readFeature(quint8 reportId, int length) {
    lastError_.clear();
    if (!dev_) {
        lastError_ = QStringLiteral("device not open");
        return {};
    }
    if (length <= 0) {
        lastError_ = QStringLiteral("invalid feature length");
        return {};
    }

    QByteArray buf(length, char(0));
    buf[0] = char(reportId);
    const int n = hid_get_feature_report(
        dev_,
        reinterpret_cast<unsigned char *>(buf.data()),
        static_cast<size_t>(buf.size()));
    if (n < 0) {
        captureError("read_feature");
        return {};
    }
    if (n > 0 && n < buf.size())
        buf.resize(n);
    return buf;
}

QStringList CadMouseHid::listManagementPaths(quint16 productId) {
    ensureInit();
    QStringList paths;
    hid_device_info *enumList = hid_enumerate(0x256f, productId);
    for (hid_device_info *info = enumList; info; info = info->next) {
        if (info->interface_number == 0)
            continue;
        if (info->path)
            paths << QString::fromUtf8(info->path);
    }
    hid_free_enumeration(enumList);
    return paths;
}

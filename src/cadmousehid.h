#ifndef CADMOUSEHID_H
#define CADMOUSEHID_H

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "hidapi.h"

/**
 * libhidapi 薄封装：按 path 打开设备，收发 Feature / Output Report。
 */
class CadMouseHid {
public:
    CadMouseHid();
    ~CadMouseHid();

    CadMouseHid(const CadMouseHid &) = delete;
    CadMouseHid &operator=(const CadMouseHid &) = delete;

    bool openPath(const QString &path);
    void close();
    bool isOpen() const { return dev_ != nullptr; }

    bool sendFeature(const QByteArray &report);
    bool sendOutput(const QByteArray &report);
    QByteArray readFeature(quint8 reportId, int length);

    QString lastError() const { return lastError_; }

    static QStringList listManagementPaths(quint16 productId);

private:
    void captureError(const char *prefix);
    static void ensureInit();

    hid_device *dev_ = nullptr;
    QString lastError_;
};

#endif // CADMOUSEHID_H

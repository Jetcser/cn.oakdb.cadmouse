#ifndef MOUSEMAINWINDOW_H
#define MOUSEMAINWINDOW_H

#include <QMainWindow>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <atomic>

QT_BEGIN_NAMESPACE
namespace Ui { class MouseMainWindow; }
class QFileSystemWatcher;
QT_END_NAMESPACE

class MouseMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MouseMainWindow(QWidget *parent = nullptr);
    ~MouseMainWindow();

private slots:
    void on_pushButtonRefresh_clicked();
    void on_pushButtonConfigure_clicked();
    void onDevicePathChanged();

private:
    Ui::MouseMainWindow *ui;
    QFileSystemWatcher *devWatcher = nullptr;
    QTimer *devDebounceTimer = nullptr;
    QTimer *statusClearTimer = nullptr;

    QString hidrawDevicePath;
    QString hidDevicePath;
    QString currentPID;
    std::atomic_bool hidBusy{false};
    /** 部分型号（如 C658 无线）不支持 Feature 0x10 GET，仅支持写入 */
    bool bulkConfigReadable = true;

    void findMouseDevice();
    void initParameter();
    void refreshStatus();
    void applyBulkConfigToUi(const QByteArray &packet);
    void showStatus(const QString &msg, bool success);
    void setupDeviceWatcher();

    QString readFileContent(const QString &filePath) const;
    QString readUeventValue(const QString &ueventPath, const QString &key) const;
    int getBatteryLevelFromsysfs() const;

    void startReadConfigAsync();
    void startConfigureAsync();
    QString classifyHidFailure(const QString &hidError) const;
};

#endif // MOUSEMAINWINDOW_H

#ifndef CADMOUSEPROTOCOL_H
#define CADMOUSEPROTOCOL_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <cstdint>

/**
 * CadMouse HID 协议常量与组包/解析辅助。
 * 32 字节 bulk Feature (report 0x10) 用于速度/轮询率/按键映射；
 * 8 字节命令 Feature (report 0x0c) 用于智能滚动与抬起检测（cadmousectl 逆向协议）。
 */
namespace CadMouseProtocol {

constexpr int kBulkReportLen = 32;
constexpr int kCmdReportLen = 8;
constexpr quint8 kBulkReportId = 0x10;
constexpr quint8 kCmdReportId = 0x0c;

constexpr int kOffSpeed = 2;
constexpr int kOffPoll = 4;
constexpr int kOffWheelRemap = 22;
constexpr int kOffGestureRemap = 25;

constexpr quint8 kWheelRemapOn = 0x0f;
constexpr quint8 kWheelRemapOff = 0x0c;
constexpr quint8 kGestureRemapOn = 0x10;
constexpr quint8 kGestureRemapOff = 0x2f;

inline QMap<int, quint8> speedByteMap() {
    return {{1, 0x0a}, {2, 0x0f}, {3, 0x14}, {4, 0x1c}, {5, 0x23}};
}

inline QMap<int, quint8> pollByteMap() {
    return {{1000, 0x01}, {500, 0x02}, {250, 0x04}, {125, 0x08}};
}

inline QMap<quint8, int> speedIndexFromByte() {
    QMap<quint8, int> m;
    const auto src = speedByteMap();
    for (auto it = src.begin(); it != src.end(); ++it)
        m.insert(it.value(), it.key());
    return m;
}

inline QMap<quint8, int> pollHzFromByte() {
    QMap<quint8, int> m;
    const auto src = pollByteMap();
    for (auto it = src.begin(); it != src.end(); ++it)
        m.insert(it.value(), it.key());
    return m;
}

struct BulkConfig {
    int speedIndex = 3;       // 1..5
    int pollHz = 1000;        // 125/250/500/1000
    bool remapWheel = true;
    bool remapGesture = true;
};

inline QByteArray buildBulkPacket(const BulkConfig &cfg) {
    QByteArray packet(kBulkReportLen, char(0x00));
    packet[0] = char(kBulkReportId);
    packet[1] = 0x00;
    packet[kOffSpeed] = char(speedByteMap().value(cfg.speedIndex, 0x14));
    packet[3] = 0x00;
    packet[kOffPoll] = char(pollByteMap().value(cfg.pollHz, 0x01));
    packet[5] = char(0xff);

    packet[17] = 0x03;
    packet[19] = 0x0a;
    packet[20] = 0x0b;
    packet[21] = 0x0c;
    packet[kOffWheelRemap] = char(cfg.remapWheel ? kWheelRemapOn : kWheelRemapOff);
    packet[23] = 0x0e;
    packet[24] = 0x0d;
    packet[kOffGestureRemap] = char(cfg.remapGesture ? kGestureRemapOn : kGestureRemapOff);
    packet[27] = 0x1e;
    packet[31] = 0x01;
    return packet;
}

inline bool parseBulkPacket(const QByteArray &packet, BulkConfig *out) {
    if (!out || packet.size() < kBulkReportLen)
        return false;
    if (static_cast<quint8>(packet[0]) != kBulkReportId)
        return false;

    const auto speedMap = speedIndexFromByte();
    const auto pollMap = pollHzFromByte();
    const quint8 speedB = static_cast<quint8>(packet[kOffSpeed]);
    const quint8 pollB = static_cast<quint8>(packet[kOffPoll]);

    if (speedMap.contains(speedB))
        out->speedIndex = speedMap.value(speedB);
    if (pollMap.contains(pollB))
        out->pollHz = pollMap.value(pollB);

    out->remapWheel = (static_cast<quint8>(packet[kOffWheelRemap]) == kWheelRemapOn);
    out->remapGesture = (static_cast<quint8>(packet[kOffGestureRemap]) == kGestureRemapOn);
    return true;
}

/** 8 字节命令：{0x0c, opt, val1, val2, 0,0,0,0} */
inline QByteArray buildCmdPacket(quint8 opt, quint8 val1, quint8 val2) {
    QByteArray p(kCmdReportLen, char(0x00));
    p[0] = char(kCmdReportId);
    p[1] = char(opt);
    p[2] = char(val1);
    p[3] = char(val2);
    return p;
}

/** 抬起检测：启用 val2=0x00，禁用 val2=0x1f */
inline QByteArray buildLiftoffPacket(bool enabled) {
    return buildCmdPacket(0x07, 0x00, enabled ? 0x00 : 0x1f);
}

/**
 * 智能滚动（checkbox 对应 mode 1 / 0）。
 * 返回按发送顺序排列的多包列表。
 */
inline QList<QByteArray> buildSmartScrollPackets(bool enabled) {
    QList<QByteArray> list;
    if (enabled) {
        list << buildCmdPacket(0x03, 0x00, 0x00);
        list << buildCmdPacket(0x04, 0x00, 0x00);
        list << buildCmdPacket(0x05, 0x00, 0x01);
    } else {
        list << buildCmdPacket(0x03, 0x00, 0x01);
        list << buildCmdPacket(0x04, 0xff, 0x00);
        list << buildCmdPacket(0x05, 0x00, 0x00);
    }
    return list;
}

inline QString packetToHexCsv(const QByteArray &packet) {
    QStringList hexList;
    hexList.reserve(packet.size());
    for (unsigned char b : packet)
        hexList << QString("0x%1").arg(b, 2, 16, QChar('0'));
    return hexList.join(',');
}

/**
 * 从 hidapitester printbuf 输出解析十六进制字节。
 * 优先截取 "read N bytes:" 之后的 dump，避免把日志里的 32、16 等十进制数字误解析进去。
 */
inline QByteArray parseHexDump(const QString &text) {
    QString dump = text;
    const int marker = text.lastIndexOf(QStringLiteral("bytes:"), -1, Qt::CaseInsensitive);
    if (marker >= 0)
        dump = text.mid(marker + 6);

    QByteArray out;
    const QRegularExpression re(QStringLiteral("\\b([0-9A-Fa-f]{2})\\b"));
    auto it = re.globalMatch(dump);
    while (it.hasNext()) {
        bool ok = false;
        const int v = it.next().captured(1).toInt(&ok, 16);
        if (ok)
            out.append(char(v & 0xff));
    }
    return out;
}

} // namespace CadMouseProtocol

#endif // CADMOUSEPROTOCOL_H

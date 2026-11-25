#include "protocol.h"

namespace rt860 {

quint8 checksum(const QByteArray &bytes) {
    quint8 sum = 0;
    for (char c : bytes) sum = quint8(sum + quint8(c));
    return sum;
}

QByteArray connectCommand() { return QByteArray("\x34\x52\x05\x10\x9b", 5); }
QByteArray endCommand() { return QByteArray("\x34\x52\x05\xee\x79", 5); }

static QByteArray header(quint8 cmd, quint16 block) {
    QByteArray p;
    p += char(cmd);
    p += char(block >> 8);
    p += char(block & 0xff);
    return p;
}

QByteArray readCommand(quint16 block) {
    QByteArray p = header(quint8(ReadOp), block);
    p += char(checksum(p));
    return p;
}

QByteArray writeCommand(quint8 cmd, quint16 index, const QByteArray &data) {
    if (data.size() != BlockSize) return {};
    QByteArray p = header(cmd, index) + data;
    p += char(checksum(p));
    return p;
}

bool isReadReply(const QByteArray &packet, quint16 block) {
    return packet.size() == PacketSize && packet.startsWith(header(quint8(ReadOp), block))
           && quint8(packet.back()) == checksum(packet.left(PacketSize - 1));
}

} // namespace rt860

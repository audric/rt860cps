// Serial packet format of the Radtel RT-860 (115200 baud, 8N1), as used by
// Radtel's CPS V1.06. See docs/design.md.
#pragma once
#include <QByteArray>

namespace rt860 {

constexpr int BlockSize = 1024;
constexpr int PacketSize = BlockSize + 4;   // command, block high, block low, data, checksum
constexpr char Ack = 0x06;
constexpr char ReadOp = 0x52;

// Sum of all bytes, modulo 256.
quint8 checksum(const QByteArray &bytes);

QByteArray connectCommand();                 // 34 52 05 10 9B
QByteArray endCommand();                     // 34 52 05 EE 79
QByteArray readCommand(quint16 block);       // 52 hi lo sum
// cmd hi lo, 1024 data bytes, sum. `data` must be exactly BlockSize long.
QByteArray writeCommand(quint8 cmd, quint16 index, const QByteArray &data);

// True if `packet` is a well-formed answer to readCommand(block).
bool isReadReply(const QByteArray &packet, quint16 block);

} // namespace rt860

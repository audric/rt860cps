// A simulated RT-860 for tests: answers the serial protocol from an in-memory image.
#pragma once
#include "protocol.h"
#include "sync.h"
#include <QList>
#include <QPair>
#include <functional>

class FakeRadio : public rt860::Transport {
public:
    QByteArray image = rt860::blankImage();
    QList<QPair<int, int>> writes;          // write command, block index
    int sessions = 0;                       // accepted connect commands
    int ends = 0;
    bool connected = false;
    int ignoreConnects = 0;                 // stay silent for this many connect commands
    int corruptReadReply = -1;              // flip a byte in the n-th read reply from now (0 = next)
    int silentAfterReads = -1;              // stop answering after this many more reads
    bool dropWrites = false;                // acknowledge writes without storing them
    std::function<void(int cmd, int index)> beforeWrite;

    void write(const QByteArray &bytes) override {
        m_in += bytes;
        for (;;) {
            if (m_in.isEmpty()) return;
            const quint8 op = quint8(m_in[0]);
            const int need = op == 0x34 ? 5 : op == 0x52 ? 4 : rt860::PacketSize;
            if (m_in.size() < need) return;
            const QByteArray p = m_in.left(need);
            m_in.remove(0, need);
            handle(op, p);
        }
    }
    QByteArray read(int) override {
        QByteArray out = m_out;
        m_out.clear();
        return out;
    }
    void discardInput() override { m_out.clear(); }

private:
    static int imageOffsetForRead(int block) {
        for (int a = 0; a < rt860::AreaCount; ++a) {
            const rt860::AreaInfo &i = rt860::areaInfo(a);
            if (i.readBlock >= 0 && block >= i.readBlock && block < i.readBlock + i.size / 1024)
                return i.offset + (block - i.readBlock) * 1024;
        }
        return -1;
    }
    void handle(quint8 op, const QByteArray &p) {
        if (op == 0x34) {
            if (p == rt860::connectCommand()) {
                if (ignoreConnects > 0) { --ignoreConnects; return; }
                connected = true;
                ++sessions;
                m_out += rt860::Ack;
            } else if (p == rt860::endCommand()) {
                connected = false;
                ++ends;
            }
            return;
        }
        if (!connected || quint8(p.back()) != rt860::checksum(p.left(p.size() - 1))) return;
        const int index = quint8(p[1]) << 8 | quint8(p[2]);
        if (op == 0x52) {
            if (silentAfterReads == 0) return;
            if (silentAfterReads > 0) --silentAfterReads;
            const int at = imageOffsetForRead(index);
            QByteArray r = p.left(3) + (at < 0 ? QByteArray(1024, '\0') : image.mid(at, 1024));
            r += char(rt860::checksum(r));
            if (corruptReadReply == 0) r[500] = char(r[500] ^ 0x40);
            if (corruptReadReply >= 0) --corruptReadReply;
            m_out += r;
            return;
        }
        for (int a = 0; a < rt860::AreaCount; ++a) {
            const rt860::AreaInfo &i = rt860::areaInfo(a);
            if (i.writeCmd != op || index >= i.size / 1024) continue;
            if (beforeWrite) beforeWrite(op, index);
            if (!dropWrites) image.replace(i.offset + index * 1024, 1024, p.mid(3, 1024));
            writes.append({op, index});
            m_out += rt860::Ack;
            return;
        }
    }
    QByteArray m_in, m_out;
};

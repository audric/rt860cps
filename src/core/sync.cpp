#include "sync.h"
#include "protocol.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>

namespace rt860 {

void Radio::connect(int attempts, int waitMs) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        m_transport.discardInput();
        m_transport.write(connectCommand());
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < waitMs) {
            const QByteArray got = m_transport.read(int(waitMs - timer.elapsed()));
            for (char c : got) {
                if (c == Ack) return;
                if (c != '\0') throw RadioError(QStringLiteral("Unexpected answer from the radio (0x%1). Is this an RT-860?").arg(quint8(c), 2, 16, QLatin1Char('0')));
            }
            if (got.isEmpty() && timer.elapsed() < waitMs) continue;
        }
    }
    throw RadioError(QStringLiteral("The radio did not answer. Check that it is switched on and the cable is pushed fully in."));
}

QByteArray Radio::readBlock(quint16 block) {
    m_transport.write(readCommand(block));
    QByteArray reply;
    QElapsedTimer timer;
    timer.start();
    while (reply.size() < PacketSize) {
        const qint64 left = m_timeoutMs - timer.elapsed();
        if (left <= 0) throw RadioError(QStringLiteral("The radio stopped answering while reading block %1.").arg(block));
        reply += m_transport.read(int(left));
    }
    if (!isReadReply(reply.left(PacketSize), block))
        throw RadioError(QStringLiteral("Damaged data received while reading block %1.").arg(block));
    return reply.mid(3, BlockSize);
}

int radioBlockCount() {
    int n = 0;
    for (int a = 0; a < AreaCount; ++a)
        if (areaInfo(a).readBlock >= 0) n += areaInfo(a).size / BlockSize;
    return n;
}

QByteArray Radio::readImage(const Progress &progress) {
    QByteArray image(ImageSize, '\0');
    const int total = radioBlockCount();
    int done = 0;
    // Same order as the CPS: settings, channels, zones, FM presets.
    for (int area : {int(Cfg), int(Channels), int(Zones), int(Fm)}) {
        const AreaInfo &info = areaInfo(area);
        for (int i = 0; i < info.size / BlockSize; ++i) {
            image.replace(info.offset + i * BlockSize, BlockSize, readBlock(quint16(info.readBlock + i)));
            if (progress) progress(++done, total);
        }
    }
    return image;
}

void Radio::writeBlock(int area, int index, const QByteArray &data) {
    const AreaInfo &info = areaInfo(area);
    if (info.writeCmd < 0 || index < 0 || index >= info.size / BlockSize || data.size() != BlockSize)
        throw RadioError(QStringLiteral("Internal error: bad write block."));
    m_transport.write(writeCommand(quint8(info.writeCmd), quint16(index), data));
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        const qint64 left = m_timeoutMs - timer.elapsed();
        if (left <= 0) throw RadioError(QStringLiteral("The radio did not confirm writing %1 block %2.").arg(QLatin1String(info.name)).arg(index));
        const QByteArray got = m_transport.read(int(left));
        if (got.isEmpty()) continue;
        if (got.front() == Ack) return;
        throw RadioError(QStringLiteral("The radio refused %1 block %2.").arg(QLatin1String(info.name)).arg(index));
    }
}

void Radio::end() { m_transport.write(endCommand()); }

static QByteArray withoutPassword(QByteArray image) {
    image.replace(CfgOffset, PasswordLength, QByteArray(PasswordLength, '\0'));
    return image;
}

QList<BlockRef> planWrites(const QByteArray &from, const QByteArray &to) {
    QList<BlockRef> plan;
    if (from.size() != ImageSize || to.size() != ImageSize) return plan;
    const QByteArray a = withoutPassword(from), b = withoutPassword(to);
    for (int area : {int(Cfg), int(Channels), int(Zones), int(Fm)}) {
        const AreaInfo &info = areaInfo(area);
        for (int i = 0; i < info.size / BlockSize; ++i) {
            const int at = info.offset + i * BlockSize;
            if (a.mid(at, BlockSize) != b.mid(at, BlockSize)) plan.append({area, i});
        }
    }
    return plan;
}

static QString saveBackup(const QString &dir, const QByteArray &image) {
    if (!QDir().mkpath(dir)) throw RadioError(QStringLiteral("Cannot create the backup folder %1. Nothing was written to the radio.").arg(dir));
    const QString stem = QStringLiteral("%1/rt860_%2").arg(dir, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss")));
    QString path = stem + QStringLiteral(".88dat");
    for (int n = 2; QFile::exists(path); ++n) path = QStringLiteral("%1_%2.88dat").arg(stem).arg(n);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(image) != image.size() || !f.flush())
        throw RadioError(QStringLiteral("Cannot save the backup %1. Nothing was written to the radio.").arg(path));
    return path;
}

WriteResult writeToRadio(Transport &transport, const QByteArray &base, const QByteArray &work, const WriteOptions &options) {
    WriteResult result;
    if (work.size() != ImageSize || (!base.isEmpty() && base.size() != ImageSize))
        throw RadioError(QStringLiteral("Internal error: bad image size."));
    if (!base.isEmpty() && planWrites(base, work).isEmpty()) {
        result.status = WriteResult::NothingToWrite;
        return result;
    }
    auto report = [&](const QString &stage, int done, int total) {
        if (options.progress) options.progress(stage, done, total);
    };

    Radio radio(transport);
    radio.connect();
    radio.readBlock(0);     // the CPS reads block 0 before anything else when writing
    const QByteArray current = radio.readImage([&](int d, int t) { report(QStringLiteral("Reading the radio"), d, t); });
    result.radioImage = current;

    // The password stays whatever the radio holds.
    QByteArray target = work;
    target.replace(CfgOffset, PasswordLength, current.mid(CfgOffset, PasswordLength));

    QList<BlockRef> plan;
    if (base.isEmpty()) {
        plan = planWrites(current, target);
    } else {
        plan = planWrites(base, target);
        for (const BlockRef &b : plan)
            if (withoutPassword(current).mid(b.imageOffset(), BlockSize) != withoutPassword(base).mid(b.imageOffset(), BlockSize))
                result.conflicts.append(b);
        if (!result.conflicts.isEmpty() && !options.overwriteConflicts) {
            radio.end();
            result.status = WriteResult::Conflict;
            return result;
        }
    }
    if (plan.isEmpty()) {
        radio.end();
        result.status = WriteResult::NothingToWrite;
        return result;
    }
    result.backupPath = saveBackup(options.backupDir, current);

    // What the radio must hold afterwards: its own data with the changed blocks replaced.
    QByteArray send = current;
    for (const BlockRef &b : plan) send.replace(b.imageOffset(), BlockSize, target.mid(b.imageOffset(), BlockSize));

    // Every block, in the order the CPS sends them: the radio erases an area when
    // it is written to, so a block left out would be lost.
    const int total = radioBlockCount();
    for (int area : {int(Cfg), int(Channels), int(Zones), int(Fm)}) {
        const AreaInfo &info = areaInfo(area);
        for (int i = 0; i < info.size / BlockSize; ++i) {
            radio.writeBlock(area, i, send.mid(info.offset + i * BlockSize, BlockSize));
            report(QStringLiteral("Writing"), ++result.blocksWritten, total);
        }
    }
    radio.end();
    result.radioImage = send;
    result.status = WriteResult::Ok;
    return result;
}

} // namespace rt860

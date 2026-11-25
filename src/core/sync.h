// Talking to the radio and writing changes back safely.
#pragma once
#include "layout.h"
#include <QByteArray>
#include <QList>
#include <QString>
#include <functional>
#include <stdexcept>

namespace rt860 {

struct RadioError : std::runtime_error {
    explicit RadioError(const QString &what) : std::runtime_error(what.toStdString()) {}
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual void write(const QByteArray &bytes) = 0;
    // Returns whatever arrived, waiting up to timeoutMs; empty on timeout.
    virtual QByteArray read(int timeoutMs) = 0;
    virtual void discardInput() = 0;
};

class Radio {
public:
    using Progress = std::function<void(int done, int total)>;
    explicit Radio(Transport &transport, int timeoutMs = 3000) : m_transport(transport), m_timeoutMs(timeoutMs) {}
    // Puts the radio in programming mode. The CPS tries 3 times, 200 ms apart.
    void connect(int attempts = 3, int waitMs = 200);
    QByteArray readBlock(quint16 block);               // 1024 bytes
    // The whole configuration as an image (.88dat layout); the Time area is zero.
    QByteArray readImage(const Progress &progress = {});
    void writeBlock(int area, int index, const QByteArray &data);
    void end();

private:
    Transport &m_transport;
    int m_timeoutMs;
};

// One 1 KiB block of an area.
struct BlockRef {
    int area = 0;
    int index = 0;
    bool operator==(const BlockRef &) const = default;
    int imageOffset() const { return areaInfo(area).offset + index * 1024; }
};
// Number of 1 KiB blocks the radio holds (and readImage reads).
int radioBlockCount();
// Blocks that differ between two images, in the order the CPS writes them.
// The read/write password (CFG[0..11]) is ignored.
QList<BlockRef> planWrites(const QByteArray &from, const QByteArray &to);

struct WriteOptions {
    QString backupDir;
    bool overwriteConflicts = false;
    int restartWaitMs = 1500;       // pause before reconnecting to verify
    int reconnectAttempts = 40;     // 200 ms apart
    std::function<void(const QString &stage, int done, int total)> progress;
};
struct WriteResult {
    enum Status { Ok, NothingToWrite, Conflict, VerifyFailed };
    Status status = Ok;
    QString backupPath;
    QList<BlockRef> conflicts;      // blocks the radio changed since `base` was read
    QList<BlockRef> failed;         // blocks that read back differently
    QByteArray radioImage;          // radio memory after the operation
    int blocksWritten = 0;
};
// base: image as last read from the radio, or empty if the edited image came
// from a file (then everything that differs from the radio is written).
// work: the edited image.
WriteResult writeToRadio(Transport &transport, const QByteArray &base, const QByteArray &work, const WriteOptions &options);

} // namespace rt860

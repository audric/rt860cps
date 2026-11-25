// Memory layout of the Radtel RT-860 as handled by Radtel's CPS V1.06
// (decompiled; see docs/design.md for the byte tables).
#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace rt860 {

// The image is Radtel's .88dat file: five areas in this order.
enum Area { Cfg, Channels, Fm, Time, Zones, AreaCount };
struct AreaInfo {
    const char *name;
    int offset;       // in the image
    int size;
    int readBlock;    // first 1 KiB block for the read command; -1 = not in the radio
    int writeCmd;     // command byte for writing; -1 = not in the radio
};
const AreaInfo &areaInfo(int area);

constexpr int CfgOffset = 0, CfgSize = 4096;
constexpr int ChannelsOffset = 4096, ChannelsSize = 49152;
constexpr int FmOffset = 53248, FmSize = 4096;
constexpr int TimeOffset = 57344, TimeSize = 32768;
constexpr int ZonesOffset = 90112, ZonesSize = 131072;
constexpr int ImageSize = 221184;

constexpr int ChannelCount = 1024, ChannelSize = 48;
constexpr int ZoneCount = 256, ZoneSize = 512, ZoneSlots = 200;
constexpr int DtmfCount = 20, DtmfMaxLength = 14;
constexpr int PasswordLength = 12;            // CFG[0..11], never changed by this program
constexpr quint32 MinFreq = 1800000, MaxFreq = 100000000;   // 18 MHz .. 1000 MHz in 10 Hz units

// A new, empty configuration with the CPS's default settings.
QByteArray blankImage();

// ---- text fields: GBK, padded with FF
QString decodeName(const QByteArray &bytes);
// At most maxBytes long, cut on a whole character, padded with FF to maxBytes.
QByteArray encodeName(const QString &text, int maxBytes);

// ---- tones: 0 none, 0x1000|tenths of Hz, 0x2000|code DCS normal, 0x3000|code DCS inverted
QString toneText(quint16 tone);                 // "", "88.5", "D023N", "D023I"
bool parseTone(const QString &text, quint16 &tone);
bool toneValid(quint16 tone);
QStringList allToneTexts();

// ---- channels
struct Channel {
    bool empty = true;
    quint32 rxFreq = 0;     // 10 Hz units
    quint32 txFreq = 0;
    quint16 rxTone = 0;
    quint16 txTone = 0;
    int mode = 0;           // modeNames()
    int bandwidth = 0;      // bandwidthNames()
    int power = 1;          // powerNames()
    int busyLock = 0;       // busyLockNames()
    int tot = 0;            // timeNames(), first 32 only
    int toneMode = 0;       // toneModeNames()
    int scrambler = 0;      // scramblerNames()
    int tailTone = 0;       // tailToneNames()
    bool scanSkip = false;
    quint32 muteCode = 0;
    QString name;

    bool operator==(const Channel &) const = default;
};
Channel decodeChannel(const QByteArray &image, int index);
// Writes the channel, keeping bytes this program does not understand.
// An empty channel is erased to FF.
void encodeChannel(QByteArray &image, int index, const Channel &channel);
// Human-readable reasons why the stored channel looks damaged; empty if fine.
QStringList channelProblems(const QByteArray &image, int index);

const QStringList &modeNames();
const QStringList &bandwidthNames();
const QStringList &powerNames();
const QStringList &busyLockNames();
const QStringList &timeNames();          // Off, 5, 10, 15, 30 .. 600 s
const QStringList &toneModeNames();
const QStringList &scramblerNames();
const QStringList &tailToneNames();

QString freqText(quint32 freq10Hz);                    // "145.55000"
bool parseFreq(const QString &text, quint32 &freq10Hz);  // false if not a number in 18..1000 MHz

// ---- zones
struct Zone {
    QString name;
    QList<quint16> channels;    // 0-based channel indexes, in order; at most ZoneSlots

    bool operator==(const Zone &) const = default;
};
Zone decodeZone(const QByteArray &image, int index);
void encodeZone(QByteArray &image, int index, const Zone &zone);

// ---- DTMF codes
QString dtmfCode(const QByteArray &image, int index);
// False (image untouched) if longer than 14 or with characters outside 0-9 A-D * #.
bool setDtmfCode(QByteArray &image, int index, const QString &code);

// ---- settings
struct Setting {
    enum Kind { List, Bool, Int, Text, Mhz };
    QString id;
    QString group;
    QString label;
    Kind kind = List;
    int offset = 0;         // in the CFG area
    int bytes = 1;          // Int: 1, 2 or 4 (little-endian); Mhz: 4, in 10 Hz units
    int bias = 0;           // Int: shown value = stored value + bias
    QStringList options;    // List: position = stored value
    int min = 0, max = 0;   // Int: range of the shown value
    int maxLength = 0;      // Text: bytes
};
const QList<Setting> &settings();
const QStringList &settingGroups();
const Setting *findSetting(const QString &id);
// List -> int position (-1 if out of range), Bool -> bool, Int -> int,
// Text -> QString, Mhz -> double.
QVariant settingValue(const QByteArray &image, const QString &id);
// Returns false and leaves the image untouched if the value is not acceptable.
bool setSettingValue(QByteArray &image, const QString &id, const QVariant &value);

} // namespace rt860

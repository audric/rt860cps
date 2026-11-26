#include "layout.h"
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <cstring>
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

namespace rt860 {

namespace {
#include "defaults.inc"

const AreaInfo kAreas[AreaCount] = {
    {"settings", CfgOffset, CfgSize, 8, 0x90},
    {"channels", ChannelsOffset, ChannelsSize, 16, 0x91},
    {"FM presets", FmOffset, FmSize, 856, 0x99},
    {"time", TimeOffset, TimeSize, -1, -1},
    {"zones", ZonesOffset, ZonesSize, 112, 0x92},
};

quint8 u8(const QByteArray &b, int at) { return quint8(b[at]); }
quint32 le(const QByteArray &b, int at, int n) {
    quint32 v = 0;
    for (int i = n - 1; i >= 0; --i) v = (v << 8) | u8(b, at + i);
    return v;
}
void putLe(QByteArray &b, int at, int n, quint32 v) {
    for (int i = 0; i < n; ++i) b[at + i] = char((v >> (8 * i)) & 0xff);
}

#ifdef Q_OS_WIN
// Converts between "GBK" and "UTF-8" with Windows code page 936; returns false if
// the text cannot be converted.
bool convert(const char *to, const char *, const QByteArray &in, QByteArray &out) {
    constexpr UINT Gbk = 936;
    out.clear();
    if (in.isEmpty()) return true;
    if (std::strcmp(to, "GBK") == 0) {
        const QString text = QString::fromUtf8(in);
        const wchar_t *wide = reinterpret_cast<const wchar_t *>(text.utf16());
        BOOL lost = FALSE;
        const int n = ::WideCharToMultiByte(Gbk, WC_NO_BEST_FIT_CHARS, wide, int(text.size()), nullptr, 0, nullptr, &lost);
        if (n <= 0 || lost) return false;
        out.resize(n);
        return ::WideCharToMultiByte(Gbk, WC_NO_BEST_FIT_CHARS, wide, int(text.size()), out.data(), n, nullptr, &lost) == n && !lost;
    }
    const int n = ::MultiByteToWideChar(Gbk, MB_ERR_INVALID_CHARS, in.constData(), int(in.size()), nullptr, 0);
    if (n <= 0) return false;
    QString text(n, QChar(0));
    if (::MultiByteToWideChar(Gbk, MB_ERR_INVALID_CHARS, in.constData(), int(in.size()), reinterpret_cast<wchar_t *>(text.data()), n) != n) return false;
    out = text.toUtf8();
    return true;
}
#else
// Converts with iconv; returns false if the text cannot be converted.
bool convert(const char *to, const char *from, const QByteArray &in, QByteArray &out) {
    iconv_t cd = iconv_open(to, from);
    if (cd == iconv_t(-1)) return false;
    QByteArray buf(in.size() * 4 + 8, '\0');
    char *src = const_cast<char *>(in.constData());
    size_t srcLeft = size_t(in.size());
    char *dst = buf.data();
    size_t dstLeft = size_t(buf.size());
    const size_t r = iconv(cd, &src, &srcLeft, &dst, &dstLeft);
    iconv_close(cd);
    if (r == size_t(-1)) return false;
    out = buf.left(buf.size() - int(dstLeft));
    return true;
}
#endif

// GBK bytes of each character of `text`; empty list entry never occurs.
// Returns false if a character has no GBK form.
bool toGbk(const QString &text, QList<QByteArray> &chars) {
    const QList<uint> ucs = text.toUcs4();
    for (uint cp : ucs) {
        QByteArray one;
        if (!convert("GBK", "UTF-8", QString::fromUcs4(reinterpret_cast<const char32_t *>(&cp), 1).toUtf8(), one) || one.isEmpty())
            return false;
        chars.append(one);
    }
    return true;
}

QStringList makeTimes(const QString &first) {
    QStringList l{first, "5 s", "10 s", "15 s"};
    for (int s = 30; s <= 600; s += 15) l << QStringLiteral("%1 s").arg(s);
    return l;
}

const double kCtcss[] = {67.0, 69.3, 71.9, 74.4, 77.0, 79.7, 82.5, 85.4, 88.5, 91.5, 94.8, 97.4, 100.0, 103.5, 107.2,
                         110.9, 114.8, 118.8, 123.0, 127.3, 131.8, 136.5, 141.3, 146.2, 151.4, 156.7, 159.8, 162.2,
                         165.5, 167.9, 171.3, 173.8, 177.3, 179.9, 183.5, 186.2, 189.9, 192.8, 196.6, 199.5, 203.5,
                         206.5, 210.7, 218.1, 225.7, 229.1, 233.6, 241.8, 250.3, 254.1};
const int kDcs[] = {23, 25, 26, 31, 32, 36, 43, 47, 51, 53, 54, 65, 71, 72, 73, 74, 114, 115, 116, 122, 125,
                    131, 132, 134, 143, 145, 152, 155, 156, 162, 165, 172, 174, 205, 212, 223, 225, 226, 243, 244, 245,
                    246, 251, 252, 255, 261, 263, 265, 266, 271, 274, 306, 311, 315, 325, 331, 332, 343, 346, 351, 356,
                    364, 365, 371, 411, 412, 413, 423, 431, 432, 445, 446, 452, 454, 455, 462, 464, 465, 466, 503, 506,
                    516, 523, 526, 532, 546, 565, 606, 612, 624, 627, 631, 632, 645, 654, 662, 664, 703, 712, 723, 731,
                    732, 734, 743, 754};   // written as decimal numbers whose digits are the octal code
} // namespace

const AreaInfo &areaInfo(int area) { return kAreas[area]; }

QByteArray blankImage() {
    QByteArray image(ImageSize, '\0');
    image.replace(CfgOffset, int(sizeof kCfgInit), QByteArray(reinterpret_cast<const char *>(kCfgInit), sizeof kCfgInit));
    image[CfgOffset + 12] = char(0xcd);
    image[CfgOffset + 13] = char(0xab);
    image.replace(CfgOffset + 1024, int(sizeof kVfoInit), QByteArray(reinterpret_cast<const char *>(kVfoInit), sizeof kVfoInit));
    for (int i = 2048; i < CfgSize; ++i) image[CfgOffset + i] = '\0';
    image.replace(ChannelsOffset, ChannelsSize, QByteArray(ChannelsSize, '\xff'));
    image.replace(FmOffset, FmSize, QByteArray(FmSize, '\xff'));
    image.replace(ZonesOffset, ZonesSize, QByteArray(ZonesSize, '\xff'));
    return image;
}

// ---- text

QString decodeName(const QByteArray &bytes) {
    int end = 0;
    while (end < bytes.size() && u8(bytes, end) != 0xff && bytes[end] != '\0') ++end;
    QByteArray raw = bytes.left(end), utf8;
    if (convert("UTF-8", "GBK", raw, utf8)) return QString::fromUtf8(utf8);
    // Not valid GBK (damaged or half a character): show what is printable.
    QString s;
    for (char c : raw) s += (c >= 32 && c < 127) ? QLatin1Char(c) : QLatin1Char('?');
    return s;
}

QByteArray encodeName(const QString &text, int maxBytes) {
    QByteArray out;
    const QList<uint> ucs = text.toUcs4();
    for (uint cp : ucs) {
        QList<QByteArray> one;
        if (!toGbk(QString::fromUcs4(reinterpret_cast<const char32_t *>(&cp), 1), one)) one = {QByteArray("?")};
        if (out.size() + one.first().size() > maxBytes) break;
        out += one.first();
    }
    return out + QByteArray(maxBytes - out.size(), '\xff');
}

// ---- tones

bool toneValid(quint16 tone) {
    const int kind = tone >> 12, value = tone & 0xfff;
    if (kind == 0) return value == 0;
    if (kind == 1) return value >= 500 && value <= 3000;
    return kind <= 3 && value < 512;
}

QString toneText(quint16 tone) {
    const int kind = tone >> 12, value = tone & 0xfff;
    if (kind == 1) return QStringLiteral("%1.%2").arg(value / 10).arg(value % 10);
    if (kind == 2 || kind == 3)
        return QStringLiteral("D%1%2").arg(value, 3, 8, QLatin1Char('0')).arg(kind == 2 ? QLatin1Char('N') : QLatin1Char('I'));
    return {};
}

bool parseTone(const QString &text, quint16 &tone) {
    const QString t = text.trimmed().toUpper();
    if (t.isEmpty() || t == QLatin1String("NONE") || t == QLatin1String("OFF")) {
        tone = 0;
        return true;
    }
    static const QRegularExpression dcs(QStringLiteral("^D([0-7]{3})([NI])$"));
    if (const auto m = dcs.match(t); m.hasMatch()) {
        tone = quint16((m.captured(2) == QLatin1String("N") ? 0x2000 : 0x3000) | m.captured(1).toInt(nullptr, 8));
        return true;
    }
    static const QRegularExpression ctcss(QStringLiteral("^(\\d{2,3})(?:[.,](\\d))?$"));
    if (const auto m = ctcss.match(t); m.hasMatch()) {
        const int tenths = m.captured(1).toInt() * 10 + m.captured(2).toInt();
        if (tenths < 500 || tenths > 3000) return false;
        tone = quint16(0x1000 | tenths);
        return true;
    }
    return false;
}

QStringList allToneTexts() {
    QStringList l{QString()};
    for (double f : kCtcss) l << QString::number(f, 'f', 1);
    for (QLatin1Char suffix : {QLatin1Char('N'), QLatin1Char('I')})
        for (int code : kDcs) l << QStringLiteral("D%1%2").arg(code, 3, 10, QLatin1Char('0')).arg(suffix);
    return l;
}

// ---- option lists

const QStringList &modeNames() { static const QStringList l{"FM", "AM", "SSB"}; return l; }
const QStringList &bandwidthNames() { static const QStringList l{"Wide", "Narrow"}; return l; }
const QStringList &powerNames() { static const QStringList l{"Low", "High"}; return l; }
const QStringList &busyLockNames() { static const QStringList l{"Off", "Carrier", "CT/DCS"}; return l; }
const QStringList &timeNames() { static const QStringList l = makeTimes(QStringLiteral("Off")); return l; }
const QStringList &toneModeNames() { static const QStringList l{"Normal", "Enc 1", "Enc 2", "Enc 3", "Mute code"}; return l; }
const QStringList &scramblerNames() { static const QStringList l{"Off", "1", "2", "3", "4", "5", "6", "7", "8"}; return l; }
const QStringList &tailToneNames() { static const QStringList l{"Off", "55 Hz", "120°", "180°", "240°"}; return l; }

QString freqText(quint32 f) { return QStringLiteral("%1.%2").arg(f / 100000).arg(f % 100000, 5, 10, QLatin1Char('0')); }

bool parseFreq(const QString &text, quint32 &freq) {
    static const QRegularExpression re(QStringLiteral("^(\\d{1,4})(?:[.,](\\d{0,5}))?$"));
    const auto m = re.match(text.trimmed());
    if (!m.hasMatch()) return false;
    const quint64 f = quint64(m.captured(1).toUInt()) * 100000 + m.captured(2).leftJustified(5, QLatin1Char('0')).toUInt();
    if (f < MinFreq || f > MaxFreq) return false;
    freq = quint32(f);
    return true;
}

// ---- channels

static int channelAt(int index) { return ChannelsOffset + index * ChannelSize; }

Channel decodeChannel(const QByteArray &image, int index) {
    Channel c;
    const int a = channelAt(index);
    if (u8(image, a + 2) != 1) return c;
    c.empty = false;
    c.mode = u8(image, a);
    c.bandwidth = u8(image, a + 3);
    c.rxTone = quint16(le(image, a + 4, 2));
    c.rxFreq = le(image, a + 6, 4);
    c.txFreq = le(image, a + 10, 4);
    c.txTone = quint16(le(image, a + 14, 2));
    c.power = u8(image, a + 16);
    c.busyLock = u8(image, a + 17);
    c.tot = u8(image, a + 18) & 0x1f;
    c.toneMode = u8(image, a + 18) >> 5;
    c.scrambler = u8(image, a + 19) & 0x0f;
    c.tailTone = (u8(image, a + 19) >> 4) & 7;
    c.scanSkip = u8(image, a + 19) & 0x80;
    c.muteCode = le(image, a + 20, 4);
    c.name = decodeName(image.mid(a + 32, 16));
    return c;
}

void encodeChannel(QByteArray &image, int index, const Channel &c) {
    const int a = channelAt(index);
    if (c.empty) {
        image.replace(a, ChannelSize, QByteArray(ChannelSize, '\xff'));
        return;
    }
    if (u8(image, a + 2) != 1) image.replace(a, ChannelSize, QByteArray(ChannelSize, '\0'));
    image[a] = char(c.mode);
    image[a + 1] = 1;
    image[a + 2] = 1;
    image[a + 3] = char(c.bandwidth);
    putLe(image, a + 4, 2, c.rxTone);
    putLe(image, a + 6, 4, c.rxFreq);
    putLe(image, a + 10, 4, c.txFreq);
    putLe(image, a + 14, 2, c.txTone);
    image[a + 16] = char(c.power);
    image[a + 17] = char(c.busyLock);
    image[a + 18] = char((c.tot & 0x1f) | ((c.toneMode & 7) << 5));
    image[a + 19] = char((c.scrambler & 0x0f) | ((c.tailTone & 7) << 4) | (c.scanSkip ? 0x80 : 0));
    putLe(image, a + 20, 4, c.muteCode);
    image.replace(a + 32, 16, encodeName(c.name, 16));
}

QStringList channelProblems(const QByteArray &image, int index) {
    QStringList p;
    const Channel c = decodeChannel(image, index);
    if (c.empty) return p;
    auto list = [&](int v, const QStringList &names, const char *what) {
        if (v < 0 || v >= names.size()) p << QStringLiteral("%1 holds %2, which is not a known value").arg(QLatin1String(what)).arg(v);
    };
    list(c.mode, modeNames(), "Mode");
    list(c.bandwidth, bandwidthNames(), "Width");
    list(c.power, powerNames(), "Power");
    list(c.busyLock, busyLockNames(), "Busy lock");
    list(c.toneMode, toneModeNames(), "Tone mode");
    list(c.scrambler, scramblerNames(), "Scrambler");
    list(c.tailTone, tailToneNames(), "Tail tone");
    if (c.rxFreq < MinFreq || c.rxFreq > MaxFreq) p << QStringLiteral("RX frequency is outside 18-1000 MHz");
    if (c.txFreq < MinFreq || c.txFreq > MaxFreq) p << QStringLiteral("TX frequency is outside 18-1000 MHz");
    if (!toneValid(c.rxTone)) p << QStringLiteral("RX tone is not a valid tone");
    if (!toneValid(c.txTone)) p << QStringLiteral("TX tone is not a valid tone");
    return p;
}

// ---- zones

static int zoneAt(int index) { return ZonesOffset + index * ZoneSize; }

Zone decodeZone(const QByteArray &image, int index) {
    Zone z;
    const int a = zoneAt(index);
    z.name = decodeName(image.mid(a + 4, 16));
    for (int i = 0; i < ZoneSlots; ++i) {
        const quint16 ch = quint16(le(image, a + 20 + 2 * i, 2));
        if (ch != 0xffff) z.channels.append(ch);
    }
    return z;
}

void encodeZone(QByteArray &image, int index, const Zone &z) {
    const int a = zoneAt(index);
    image.replace(a + 4, 16, encodeName(z.name, 16));
    int count = 0;
    for (int i = 0; i < ZoneSlots; ++i) {
        const quint16 ch = i < z.channels.size() ? z.channels.at(i) : quint16(0xffff);
        putLe(image, a + 20 + 2 * i, 2, ch);
        if (ch < ChannelCount) ++count;
    }
    image[a] = char(count);        // the CPS writes the low byte to both
    image[a + 1] = char(count);
}

// ---- DTMF

static int dtmfAt(int index) { return CfgOffset + 256 + index * 16; }

QString dtmfCode(const QByteArray &image, int index) {
    const int a = dtmfAt(index);
    const int length = u8(image, a + 15);
    if (length > DtmfMaxLength) return {};
    return QString::fromLatin1(image.mid(a, length));
}

bool setDtmfCode(QByteArray &image, int index, const QString &code) {
    static const QRegularExpression ok(QStringLiteral("^[0-9A-D*#]{0,14}$"));
    const QString c = code.trimmed().toUpper();
    if (!ok.match(c).hasMatch()) return false;
    const int a = dtmfAt(index);
    image.replace(a, 15, c.toLatin1() + QByteArray(15 - c.size(), '\xff'));
    image[a + 15] = char(c.size());
    return true;
}

// ---- settings

static QList<Setting> buildSettings() {
    QList<Setting> t;
    auto add = [&](Setting::Kind kind, const char *id, const char *group, const char *label, int offset) -> Setting & {
        Setting s;
        s.kind = kind;
        s.id = QLatin1String(id);
        s.group = QLatin1String(group);
        s.label = QString::fromUtf8(label);
        s.offset = offset;
        t.append(s);
        return t.last();
    };
    auto list = [&](const char *id, const char *g, const char *label, int offset, const QStringList &options) {
        add(Setting::List, id, g, label, offset).options = options;
    };
    auto flag = [&](const char *id, const char *g, const char *label, int offset) { add(Setting::Bool, id, g, label, offset); };
    auto num = [&](const char *id, const char *g, const char *label, int offset, int bytes, int min, int max, int bias = 0) {
        Setting &s = add(Setting::Int, id, g, label, offset);
        s.bytes = bytes;
        s.min = min;
        s.max = max;
        s.bias = bias;
    };
    auto text = [&](const char *id, const char *g, const char *label, int offset, int length) {
        add(Setting::Text, id, g, label, offset).maxLength = length;
    };
    auto mhz = [&](const char *id, const char *g, const char *label, int offset) { add(Setting::Mhz, id, g, label, offset).bytes = 4; };

    const QStringList offOn{"Off", "On"};
    const QStringList times = makeTimes(QStringLiteral("Off"));
    const QStringList workMode{"Frequency", "Channel", "Zone"};
    const QStringList showMode{"Channel number", "Frequency", "Channel name"};
    const QStringList lockType{"Not locked", "RX only", "Locked"};
    const QStringList keys{"None", "Monitor", "Power switch", "Dual standby", "TX priority", "Scan", "Backlight on/off",
                           "Roger beep", "FM radio", "Talkaround", "Alarm", "Frequency detect", "Remote CTCSS/DCS",
                           "Send tone", "Query state", "RX demodulation", "Spectrum", "Squelch", "Frequency step",
                           "Save channel", "VOX", "Deep sleep", "NOAA channel", "GPS", "Change channel display",
                           "Zone selection"};
    QStringList ms100;   // 0 .. 2000 ms in steps of 100
    for (int i = 0; i <= 20; ++i) ms100 << QStringLiteral("%1 ms").arg(i * 100);
    QStringList ms10;    // 30 .. 200 ms in steps of 10
    for (int i = 30; i <= 200; i += 10) ms10 << QStringLiteral("%1 ms").arg(i);
    QStringList dwell;
    for (int i = 0; i <= 30; ++i) dwell << QString::number(i);
    QStringList dtmfSelect;
    for (int i = 1; i <= 16; ++i) dtmfSelect << QStringLiteral("DTMF-%1").arg(i, 2, 10, QLatin1Char('0'));

    const char *g = "Power on";
    flag("start_picture", g, "Show the startup picture", 14);
    flag("start_beep", g, "Startup beep", 15);
    flag("start_text_on", g, "Show the startup text", 16);
    num("start_line", g, "Startup text line", 17, 1, 0, 7);
    num("start_column", g, "Startup text column", 18, 1, 0, 127);
    text("start_text", g, "Startup text", 36, 32);
    flag("start_pw_on", g, "Ask for a password at power on", 19);
    text("start_pw", g, "Power-on password", 20, 16);
    text("radio_name", g, "Radio name", 68, 16);
    flag("apo_on", g, "Automatic power off", 97);
    num("apo_seconds", g, "Power off after (seconds)", 98, 4, 0, 86399);
    flag("wake_on", g, "Automatic wake up", 102);
    num("wake_seconds", g, "Wake up after (seconds)", 103, 4, 0, 86399);

    g = "Basic";
    list("voice_prompt", g, "Voice prompt", 84, offOn);
    list("key_beep", g, "Key beep", 85, offOn);
    list("key_lock", g, "Key lock", 86, {"Unlocked", "Locked"});
    list("lock_timer", g, "Auto lock timer", 87, times);
    list("save_mode", g, "Battery save", 91, {"Off", "1:1", "1:2", "1:3"});
    list("save_start", g, "Battery save starts after", 92, makeTimes(QStringLiteral("0 s")));
    list("menu_timer", g, "Menu exit timer", 93, times);
    list("dual_standby", g, "Dual standby", 94, offOn);
    list("dual_display", g, "Display", 253, {"Dual", "Single"});
    list("talkaround", g, "Talkaround", 95, {"Off", "Talkaround", "Inverse"});
    list("alarm_type", g, "Alarm", 96, {"Local", "Remote", "Local + remote"});
    list("tx_priority", g, "TX priority", 119, {"Edit", "Busy"});
    list("main_ptt", g, "Main PTT transmits on", 120, {"Area A", "Main area"});
    list("step", g, "Frequency step", 121,
         {"0.25 kHz", "1.25 kHz", "2.5 kHz", "5 kHz", "6.25 kHz", "10 kHz", "12.5 kHz", "20 kHz", "25 kHz", "50 kHz", "100 kHz",
          "500 kHz", "1 MHz", "5 MHz"});
    num("squelch", g, "Squelch level", 223, 1, 0, 10);
    list("vox", g, "VOX", 232, offOn);
    num("vox_threshold", g, "VOX threshold", 233, 1, 0, 255);
    list("vox_delay", g, "VOX delay", 234, {"0 s", "0.5 s", "1 s", "1.5 s", "2 s", "2.5 s", "3 s", "3.5 s", "4 s", "4.5 s", "5 s"});
    list("noaa", g, "NOAA weather alert", 238, offOn);
    list("relay_delay", g, "Repeater tail delay", 236, ms100);
    list("detect_range", g, "Frequency detect range", 235,
         {"18-64 MHz", "64-136 MHz", "136-174 MHz", "174-240 MHz", "240-320 MHz", "320-400 MHz", "400-480 MHz", "480-560 MHz",
          "560-640 MHz", "840-920 MHz", "920-1000 MHz"});
    flag("sptt", g, "SPTT (name used by the Radtel CPS)", 157);
    list("six_or_eight", g, "6 or 8 (name used by the Radtel CPS)", 251, {"6", "8"});

    g = "Display and sound";
    list("backlight", g, "Backlight", 88, offOn);
    list("backlight_timer", g, "Display timer", 89, times);
    list("saver_timer", g, "LCD saver timer", 90, times);
    num("contrast", g, "LCD contrast", 220, 1, 0, 30);
    num("brightness", g, "Brightness", 237, 1, 0, 6);
    list("rssi_refresh", g, "Signal meter refresh", 252, ms100);
    num("mic_gain", g, "Microphone gain", 226, 1, 0, 31);
    num("spk_gain", g, "Speaker gain", 227, 1, 0, 63);
    num("am_gain", g, "AM demodulation gain", 228, 1, 0, 15);
    num("glitch", g, "Glitch threshold", 229, 1, 0, 10);
    list("tx_start_beep", g, "Beep at start of transmission", 230, offOn);
    list("roger", g, "Roger beep", 231, {"Off", "Roger 1", "Roger 2", "MDC1200"});
    num("single_tone", g, "Single tone (Hz)", 221, 2, 0, 20000);
    num("cw_tone", g, "CW tone (Hz)", 254, 2, 0, 20000);
    list("fm_standby", g, "Keep receiving while the FM radio plays", 239, offOn);
    num("fm_channel", g, "FM radio channel", 240, 1, 1, 128, 1);

    g = "Areas A and B";
    list("main_area", g, "Main area", 122, {"A", "B"});
    list("a_mode", g, "Area A mode", 123, workMode);
    list("a_show", g, "Area A shows", 124, showMode);
    num("a_zone", g, "Area A zone", 125, 1, 1, 250, 1);
    // Seen on a radio: a number past the end of the zone lands on the zone's last channel.
    num("a_channel", g, "Area A channel (position in its zone)", 126, 2, 1, ZoneSlots, 1);
    list("b_mode", g, "Area B mode", 128, workMode);
    list("b_show", g, "Area B shows", 129, showMode);
    num("b_zone", g, "Area B zone", 130, 1, 1, 250, 1);
    num("b_channel", g, "Area B channel (position in its zone)", 131, 2, 1, ZoneSlots, 1);

    g = "Scanning";
    list("scan_dir", g, "Direction", 153, {"Up", "Down"});
    list("scan_mode", g, "Mode", 154, {"Carrier (CO)", "Time (TO)", "Search (SE)"});
    list("scan_return", g, "After scanning return to", 155, {"Original channel", "Current channel"});
    list("scan_dwell", g, "Dwell", 156, dwell);
    mhz("scan_start", g, "Scan range start (MHz)", 108);
    mhz("scan_end", g, "Scan range end (MHz)", 112);

    g = "Lock ranges";
    for (int i = 0; i < 4; ++i) {
        const int at = 133 + 5 * i;
        const QByteArray n = QByteArray::number(i + 1);
        list(("lock" + n + "_type").constData(), g, ("Range " + n + " lock").constData(), at, lockType);
        num(("lock" + n + "_start").constData(), g, ("Range " + n + " from (MHz)").constData(), at + 1, 2, 18, 1000);
        num(("lock" + n + "_end").constData(), g, ("Range " + n + " to (MHz)").constData(), at + 3, 2, 18, 1000);
    }

    g = "Keys";
    list("fs1_short", g, "FS-1, short press", 158, keys);
    list("fs1_long", g, "FS-1, long press", 159, keys);
    list("fs2_short", g, "FS-2, short press", 160, keys);
    list("fs2_long", g, "FS-2, long press", 161, keys);
    list("alarm_short", g, "Alarm key, short press", 162, keys);
    list("alarm_long", g, "Alarm key, long press", 163, keys);
    for (int i = 0; i < 10; ++i) {
        const QByteArray n = QByteArray::number(i);
        list(("key" + n).constData(), g, ("Key " + n).constData(), 164 + i, keys);
    }

    g = "DTMF";
    list("dtmf_delay", g, "Send delay", 241, ms100);
    list("dtmf_duration", g, "Send duration", 242, ms10);
    list("dtmf_interval", g, "Send interval", 243, ms10);
    list("dtmf_mode", g, "Send mode", 244, {"Off", "TX begin", "TX end", "Begin and end"});
    list("dtmf_select", g, "Code to send", 245, dtmfSelect);
    list("dtmf_display", g, "Show received codes", 246, offOn);
    num("dtmf_gain", g, "Encode gain", 247, 1, 0, 127);
    num("dtmf_threshold", g, "Decode threshold", 248, 1, 0, 63);
    list("remote_control", g, "Remote control", 249, offOn);
    list("remote_time", g, "Remote time calibration", 250, offOn);

    g = "GPS and Bluetooth";
    list("gps", g, "GPS", 174, offOn);
    list("gps_baud", g, "GPS baud rate", 175, {"4800", "9600", "14400", "19200", "38400", "56000", "57600", "115200", "128000", "256000"});
    list("utc", g, "Time zone", 176,
         {"UTC 0", "UTC+1", "UTC+2", "UTC+3", "UTC+3.5", "UTC+4", "UTC+5", "UTC+5.5", "UTC+6", "UTC+7", "UTC+8", "UTC+9", "UTC+10",
          "UTC+11", "UTC+12", "UTC-1", "UTC-2", "UTC-3", "UTC-4", "UTC-5", "UTC-6", "UTC-7", "UTC-8", "UTC-9", "UTC-10", "UTC-11",
          "UTC-12"});
    flag("bt", g, "Bluetooth", 181);
    text("bt_name", g, "Bluetooth name", 182, 16);
    flag("bt_pin_on", g, "Bluetooth PIN required", 214);
    text("bt_pin", g, "Bluetooth PIN", 198, 16);
    list("bt_mode", g, "Bluetooth mode", 215, {"Audio", "Programming"});
    flag("bt_mic", g, "Bluetooth microphone", 216);
    flag("bt_spk", g, "Bluetooth speaker", 217);
    num("bt_mic_gain", g, "Bluetooth microphone gain", 218, 1, 0, 255);
    num("bt_spk_gain", g, "Bluetooth speaker gain", 219, 1, 0, 255);
    return t;
}

const QList<Setting> &settings() {
    static const QList<Setting> t = buildSettings();
    return t;
}

const QStringList &settingGroups() {
    static const QStringList g = [] {
        QStringList l;
        for (const Setting &s : settings())
            if (!l.contains(s.group)) l << s.group;
        return l;
    }();
    return g;
}

const Setting *findSetting(const QString &id) {
    for (const Setting &s : settings())
        if (s.id == id) return &s;
    return nullptr;
}

QVariant settingValue(const QByteArray &image, const QString &id) {
    const Setting *s = findSetting(id);
    if (!s || image.size() != ImageSize) return {};
    const int a = CfgOffset + s->offset;
    switch (s->kind) {
    case Setting::List: {
        const int v = u8(image, a);
        return v < s->options.size() ? v : -1;
    }
    case Setting::Bool: return u8(image, a) != 0;
    case Setting::Int: return int(le(image, a, s->bytes)) + s->bias;
    case Setting::Text: return decodeName(image.mid(a, s->maxLength));
    case Setting::Mhz: return le(image, a, 4) / 100000.0;
    }
    return {};
}

bool setSettingValue(QByteArray &image, const QString &id, const QVariant &value) {
    const Setting *s = findSetting(id);
    if (!s || image.size() != ImageSize) return false;
    const int a = CfgOffset + s->offset;
    switch (s->kind) {
    case Setting::List: {
        bool ok = false;
        const int v = value.toInt(&ok);
        if (!ok || v < 0 || v >= s->options.size()) return false;
        image[a] = char(v);
        return true;
    }
    case Setting::Bool:
        image[a] = value.toBool() ? 1 : 0;
        return true;
    case Setting::Int: {
        bool ok = false;
        const int v = value.toInt(&ok);
        if (!ok || v < s->min || v > s->max) return false;
        putLe(image, a, s->bytes, quint32(v - s->bias));
        return true;
    }
    case Setting::Text: {
        QList<QByteArray> chars;
        if (!toGbk(value.toString(), chars)) return false;
        int total = 0;
        for (const QByteArray &c : chars) total += c.size();
        if (total > s->maxLength) return false;
        image.replace(a, s->maxLength, encodeName(value.toString(), s->maxLength));
        return true;
    }
    case Setting::Mhz: {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (!ok || v < 18.0 || v > 999.99999) return false;
        putLe(image, a, 4, quint32(qRound64(v * 100000.0)));
        return true;
    }
    }
    return false;
}

} // namespace rt860

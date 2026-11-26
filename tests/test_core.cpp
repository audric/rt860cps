#include "fakeradio.h"
#include "layout.h"
#include "presets.h"
#include "protocol.h"
#include "sync.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace rt860;

static QByteArray hex(const char *h) { return QByteArray::fromHex(h); }

static Channel sampleChannel() {
    Channel c;
    c.empty = false;
    c.rxFreq = 14550000;
    c.txFreq = 14490000;
    quint16 t = 0;
    parseTone("88.5", t);
    c.txTone = t;
    c.mode = 0;
    c.bandwidth = 1;
    c.power = 1;
    c.busyLock = 2;
    c.tot = 7;
    c.toneMode = 4;
    c.scrambler = 8;
    c.tailTone = 3;
    c.scanSkip = true;
    c.muteCode = 0x12345678;
    c.name = "RPT VHF";
    return c;
}

// An image with some content, as if read from a radio.
static QByteArray sampleImage() {
    QByteArray img = blankImage();
    encodeChannel(img, 0, sampleChannel());
    Channel c = sampleChannel();
    c.name = "SECOND";
    c.rxFreq = c.txFreq = 43350000;
    encodeChannel(img, 1, c);
    c.name = "FAR";
    encodeChannel(img, 1000, c);
    encodeZone(img, 0, Zone{"Home", {0, 1}});
    img.replace(CfgOffset, PasswordLength, QByteArray("SECRETSECRET"));
    return img;
}

static WriteOptions fastOptions(const QTemporaryDir &dir) {
    WriteOptions o;
    o.backupDir = dir.path();
    o.restartWaitMs = 0;
    return o;
}

class TestCore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { setPresetDirs({QStringLiteral(RT860_LISTS_SOURCE_DIR)}); }
    // ---- packets (bytes taken from Radtel CPS V1.06)
    void connectAndEndCommands() {
        QCOMPARE(connectCommand(), hex("345205109b"));
        QCOMPARE(endCommand(), hex("345205ee79"));
    }
    void readCommandMatchesCps() {
        QCOMPARE(readCommand(8), hex("5200085a"));     // literal in the CPS
        QCOMPARE(readCommand(0), hex("52000052"));     // literal in the CPS
        QCOMPARE(readCommand(856), hex("520358ad"));
    }
    void writeCommandHasHeaderDataAndSum() {
        const QByteArray data(BlockSize, '\x01');
        const QByteArray p = writeCommand(0x91, 0x0102, data);
        QCOMPARE(p.size(), 1028);
        QCOMPARE(p.left(3), hex("910102"));
        QCOMPARE(p.mid(3, BlockSize), data);
        QCOMPARE(quint8(p.back()), quint8((0x91 + 1 + 2 + 1024) & 0xff));
        QVERIFY(writeCommand(0x91, 0, QByteArray(10, '\0')).isEmpty());
    }
    void readReplyChecks() {
        QByteArray r = hex("520010") + QByteArray(BlockSize, '\x07');
        r += char(checksum(r));
        QVERIFY(isReadReply(r, 16));
        QVERIFY(!isReadReply(r, 17));
        r[100] = 0;
        QVERIFY(!isReadReply(r, 16));
        QVERIFY(!isReadReply(r.left(1000), 16));
    }

    // ---- image
    void areasAddUpToThe88datFile() {
        QCOMPARE(blankImage().size(), 221184);
        int total = 0;
        for (int a = 0; a < AreaCount; ++a) {
            QCOMPARE(areaInfo(a).offset, total);
            total += areaInfo(a).size;
        }
        QCOMPARE(total, ImageSize);
        QCOMPARE(radioBlockCount(), 184);
    }
    void blankImageHasCpsDefaults() {
        const QByteArray img = blankImage();
        QCOMPARE(img.mid(12, 2), hex("cdab"));
        QCOMPARE(settingValue(img, "start_pw").toString(), QString("123456"));
        QCOMPARE(settingValue(img, "start_text").toString(), QString("Welcome"));
        QCOMPARE(settingValue(img, "radio_name").toString(), QString());
        QCOMPARE(settingValue(img, "bt_name").toString(), QString("R6000"));
        for (int i = 0; i < ChannelCount; ++i) QVERIFY(decodeChannel(img, i).empty);
        QVERIFY(decodeZone(img, 5).channels.isEmpty());
        QCOMPARE(dtmfCode(img, 0), QString());
    }
    void everySettingReadsTheCpsDefaults() {
        const QByteArray img = blankImage();
        QStringList ids;
        for (const Setting &s : settings()) {
            QVERIFY2(!ids.contains(s.id), qPrintable(s.id));
            ids << s.id;
            const QVariant v = settingValue(img, s.id);
            QVERIFY2(v.isValid(), qPrintable(s.id));
            if (s.kind == Setting::List) QVERIFY2(v.toInt() >= 0, qPrintable(s.id));
            if (s.kind == Setting::Int) QVERIFY2(v.toInt() >= s.min && v.toInt() <= s.max, qPrintable(s.id + " = " + v.toString()));
            if (s.kind == Setting::Mhz) QVERIFY2(v.toDouble() >= 18.0 && v.toDouble() <= 1000.0, qPrintable(s.id));
            // writing the value back must not change anything
            QByteArray copy = img;
            QVERIFY2(setSettingValue(copy, s.id, v), qPrintable(s.id));
            QVERIFY2(copy == img, qPrintable(s.id));
        }
    }
    void settingsDoNotOverlap() {
        QList<int> used(256, 0);
        for (const Setting &s : settings()) {
            const int n = s.kind == Setting::Text ? s.maxLength : s.kind == Setting::Mhz ? 4 : s.kind == Setting::Int ? s.bytes : 1;
            QVERIFY2(s.offset >= 14 && s.offset + n <= 256, qPrintable(s.id));
            for (int i = 0; i < n; ++i) QVERIFY2(used[s.offset + i]++ == 0, qPrintable(s.id));
        }
    }
    void settingsRoundTrip() {
        QByteArray img = blankImage();
        QVERIFY(setSettingValue(img, "squelch", 7));
        QCOMPARE(quint8(img[223]), quint8(7));
        QVERIFY(!setSettingValue(img, "squelch", 11));
        QVERIFY(setSettingValue(img, "a_channel", 300));          // shown 1-based, stored 0-based, 16-bit
        QCOMPARE(img.mid(126, 2), hex("2b01"));
        QCOMPARE(settingValue(img, "a_channel").toInt(), 300);
        QVERIFY(setSettingValue(img, "scan_start", 144.0));
        QCOMPARE(img.mid(108, 4), hex("00badb00"));               // 14400000
        QVERIFY(!setSettingValue(img, "scan_start", 5.0));
        QVERIFY(setSettingValue(img, "roger", 3));
        QVERIFY(!setSettingValue(img, "roger", 4));
        QVERIFY(setSettingValue(img, "bt", true));
        QCOMPARE(quint8(img[181]), quint8(1));
        QVERIFY(setSettingValue(img, "radio_name", "IU0XYZ"));
        QCOMPARE(img.mid(68, 16), QByteArray("IU0XYZ") + QByteArray(10, '\xff'));
        QVERIFY(!setSettingValue(img, "radio_name", "12345678901234567"));
        QVERIFY(setSettingValue(img, "apo_seconds", 3600));
        QCOMPARE(img.mid(98, 4), hex("100e0000"));
    }
    void outOfRangeListValueIsReported() {
        QByteArray img = blankImage();
        img[231] = 9;
        QCOMPARE(settingValue(img, "roger").toInt(), -1);
    }

    // ---- tones, frequencies, names
    void tones() {
        quint16 t = 0;
        QVERIFY(parseTone("88.5", t));
        QCOMPARE(t, quint16(0x1375));
        QVERIFY(parseTone("D023N", t));
        QCOMPARE(t, quint16(0x2013));
        QVERIFY(parseTone("d023i", t));
        QCOMPARE(t, quint16(0x3013));
        QVERIFY(parseTone("100", t));
        QCOMPARE(t, quint16(0x1000 | 1000));
        QVERIFY(parseTone("", t));
        QCOMPARE(t, quint16(0));
        QVERIFY(!parseTone("D089N", t));
        QVERIFY(!parseTone("12.3", t));
        QVERIFY(!parseTone("hello", t));
        QCOMPARE(toneText(0x1375), QString("88.5"));
        QCOMPARE(toneText(0x2013), QString("D023N"));
        QCOMPARE(toneText(0x31ec), QString("D754I"));
        QCOMPARE(toneText(0), QString());
        const QStringList all = allToneTexts();
        QCOMPARE(all.size(), 1 + 50 + 210);                       // same count as the CPS list
        for (const QString &s : all) {
            QVERIFY2(parseTone(s, t), qPrintable(s));
            QCOMPARE(toneText(t), s);
            QVERIFY(toneValid(t));
        }
        QVERIFY(!toneValid(0x4000));
        QVERIFY(!toneValid(0x0005));
    }
    void frequencies() {
        quint32 f = 0;
        QVERIFY(parseFreq("145.55", f));
        QCOMPARE(f, 14555000u);
        QVERIFY(parseFreq("433,5", f));
        QCOMPARE(f, 43350000u);
        QVERIFY(parseFreq("1000", f));
        QVERIFY(!parseFreq("1000.00001", f));
        QVERIFY(!parseFreq("17.99999", f));
        QVERIFY(!parseFreq("abc", f));
        QVERIFY(!parseFreq("145.555555", f));
        QCOMPARE(freqText(14555000), QString("145.55000"));
        QCOMPARE(freqText(1800000), QString("18.00000"));
    }
    void namesAreGbkAndCutOnWholeCharacters() {
        QCOMPARE(encodeName("AB", 4), QByteArray("AB\xff\xff", 4));
        QCOMPARE(encodeName(QString::fromUtf8("中文"), 16).left(4), hex("d6d0cec4"));
        QCOMPARE(decodeName(hex("d6d0cec4ffff")), QString::fromUtf8("中文"));
        // 15 ASCII + one 2-byte character does not fit in 16: the character is dropped whole
        const QByteArray cut = encodeName(QString("123456789012345") + QString::fromUtf8("中"), 16);
        QCOMPARE(cut, QByteArray("123456789012345\xff", 16));
        QCOMPARE(decodeName(encodeName("A B", 16)), QString("A B"));
        QCOMPARE(decodeName(hex("41d6ff")), QString("A?"));       // half a character: no crash
    }

    // ---- channels
    void cpsDefaultVfoRecordsDecode() {
        // The VFO records have the channel layout; copy them over two channels.
        QByteArray img = blankImage();
        img.replace(ChannelsOffset, 96, img.mid(CfgOffset + 1024, 96));
        const Channel a = decodeChannel(img, 0), b = decodeChannel(img, 1);
        QVERIFY(!a.empty && !b.empty);
        QCOMPARE(freqText(a.rxFreq), QString("400.55500"));
        QCOMPARE(freqText(a.txFreq), QString("400.55500"));
        QCOMPARE(freqText(b.rxFreq), QString("145.55500"));
        QCOMPARE(powerNames().at(a.power), QString("High"));
        QCOMPARE(a.rxTone, quint16(0));
        QVERIFY(channelProblems(img, 0).isEmpty());
        QVERIFY(channelProblems(img, 1).isEmpty());
    }
    void channelRoundTripAndBytes() {
        QByteArray img = blankImage();
        const Channel c = sampleChannel();
        encodeChannel(img, 3, c);
        QCOMPARE(decodeChannel(img, 3), c);
        const QByteArray r = img.mid(ChannelsOffset + 3 * 48, 48);
        QCOMPARE(r.left(4), hex("00010101"));                      // FM, 1, in use, narrow
        QCOMPARE(r.mid(4, 2), hex("0000"));                        // no RX tone
        QCOMPARE(r.mid(6, 4), hex("f003de00"));                    // 14550000
        QCOMPARE(r.mid(10, 4), hex("9019dd00"));                   // 14490000
        QCOMPARE(r.mid(14, 2), hex("7513"));                       // 88.5
        QCOMPARE(r.mid(16, 2), hex("0102"));                       // high, busy lock CT/DCS
        QCOMPARE(quint8(r[18]), quint8(7 | 4 << 5));
        QCOMPARE(quint8(r[19]), quint8(8 | 3 << 4 | 0x80));
        QCOMPARE(r.mid(20, 4), hex("78563412"));
        QCOMPARE(r.mid(24, 8), QByteArray(8, '\0'));
        QCOMPARE(r.mid(32), QByteArray("RPT VHF") + QByteArray(9, '\xff'));
        QVERIFY(channelProblems(img, 3).isEmpty());
        QVERIFY(decodeChannel(img, 2).empty && decodeChannel(img, 4).empty);
    }
    void emptyChannelIsAllFF() {
        QByteArray img = blankImage();
        encodeChannel(img, 3, sampleChannel());
        encodeChannel(img, 3, Channel{});
        QCOMPARE(img.mid(ChannelsOffset + 3 * 48, 48), QByteArray(48, '\xff'));
    }
    void unknownBytesSurviveAnEdit() {
        QByteArray img = blankImage();
        encodeChannel(img, 3, sampleChannel());
        img.replace(ChannelsOffset + 3 * 48 + 24, 8, hex("0102030405060708"));
        Channel c = decodeChannel(img, 3);
        c.name = "NEW";
        encodeChannel(img, 3, c);
        QCOMPARE(img.mid(ChannelsOffset + 3 * 48 + 24, 8), hex("0102030405060708"));
    }
    void damagedChannelIsReportedAndKept() {
        QByteArray img = blankImage();
        encodeChannel(img, 3, sampleChannel());
        const int a = ChannelsOffset + 3 * 48;
        img[a] = 7;                                                // mode 7
        img.replace(a + 6, 4, hex("01000000"));                    // 10 Hz
        img.replace(a + 4, 2, hex("0050"));                        // tone kind 5
        const QStringList p = channelProblems(img, 3);
        QCOMPARE(p.size(), 3);
        QVERIFY(p.join(' ').contains("Mode"));
        const QByteArray before = img;
        encodeChannel(img, 3, decodeChannel(img, 3));              // re-encoding changes nothing
        QCOMPARE(img, before);
    }

    // ---- zones and DTMF
    void zoneRoundTripAndBytes() {
        QByteArray img = blankImage();
        const Zone z{"Home", {5, 0, 1023}};
        encodeZone(img, 2, z);
        QCOMPARE(decodeZone(img, 2), z);
        const QByteArray r = img.mid(ZonesOffset + 2 * 512, 512);
        QCOMPARE(r.left(4), hex("0303ffff"));                      // count twice, bytes 2-3 untouched
        QCOMPARE(r.mid(4, 16), QByteArray("Home") + QByteArray(12, '\xff'));
        QCOMPARE(r.mid(20, 8), hex("05000000ff03ffff"));
        QCOMPARE(r.mid(20 + 6, 400 - 6), QByteArray(394, '\xff'));
        QCOMPARE(r.mid(420), QByteArray(92, '\xff'));              // rest of the record untouched
    }
    void zoneKeepsUnknownChannelNumbersButDoesNotCountThem() {
        QByteArray img = blankImage();
        encodeZone(img, 0, Zone{"X", {1, 2000}});
        QCOMPARE(decodeZone(img, 0).channels, (QList<quint16>{1, 2000}));
        QCOMPARE(quint8(img[ZonesOffset]), quint8(1));
    }
    void zoneHolds200Channels() {
        QByteArray img = blankImage();
        Zone z{"Big", {}};
        for (int i = 0; i < 230; ++i) z.channels << quint16(i);
        encodeZone(img, 255, z);
        QCOMPARE(decodeZone(img, 255).channels.size(), 200);
        QCOMPARE(quint8(img[ZonesOffset + 255 * 512]), quint8(200));
        QCOMPARE(img.size(), ImageSize);
    }
    void dtmfCodes() {
        QByteArray img = blankImage();
        QVERIFY(setDtmfCode(img, 1, "123a*#"));
        QCOMPARE(dtmfCode(img, 1), QString("123A*#"));
        QCOMPARE(img.mid(256 + 16, 16), QByteArray("123A*#") + QByteArray(9, '\xff') + QByteArray(1, '\x06'));
        QVERIFY(setDtmfCode(img, 19, "12345678901234"));
        QCOMPARE(quint8(img[256 + 19 * 16 + 15]), quint8(14));
        const QByteArray before = img;
        QVERIFY(!setDtmfCode(img, 1, "123456789012345"));
        QVERIFY(!setDtmfCode(img, 1, "12E"));
        QCOMPARE(img, before);
        QVERIFY(setDtmfCode(img, 1, ""));
        QCOMPARE(dtmfCode(img, 1), QString());
    }

    // ---- preset channel lists
    void presetGroupsAreWellFormed() {
        QStringList ids;
        for (const PresetGroup &g : presetGroups()) {
            QVERIFY2(!ids.contains(g.id), qPrintable(g.id));
            ids << g.id;
            QVERIFY2(!g.channels.isEmpty() && g.channels.size() <= ZoneSlots, qPrintable(g.id));
            QCOMPARE(decodeName(encodeName(g.name, 16)), g.name);          // fits as a zone name
            QByteArray img = blankImage();
            QStringList names;
            QList<QPair<quint32, int>> seen;
            for (int i = 0; i < g.channels.size(); ++i) {
                const Channel &c = g.channels.at(i);
                encodeChannel(img, i, c);
                QVERIFY2(decodeChannel(img, i) == c, qPrintable(c.name));   // name fits, values storable
                QVERIFY2(channelProblems(img, i).isEmpty(), qPrintable(c.name));
                QVERIFY2(!names.contains(c.name), qPrintable(c.name));
                names << c.name;
                // repeaters of a region and satellite Doppler steps share frequencies; elsewhere a repeat is a mistake
                QVERIFY2(g.id.startsWith("rpt_it_") || g.id == "satellites" || !seen.contains({c.rxFreq, c.mode}), qPrintable(c.name));
                seen.append({c.rxFreq, c.mode});
            }
        }
        for (const char *id : {"cb_fm", "cb_am", "marine", "pmr446", "lpd433", "freenet"}) QVERIFY2(findPresetGroup(id), id);
        QVERIFY(!findPresetGroup("nope"));
    }
    void presetFrequencies() {
        auto ch = [](const char *group, const QString &name) {
            for (const Channel &c : findPresetGroup(group)->channels)
                if (c.name == name) return c;
            qFatal("no channel %s", qPrintable(name));
            return Channel{};
        };
        auto rx = [&](const char *g, const char *n) { return freqText(ch(g, n).rxFreq); };
        auto tx = [&](const char *g, const char *n) { return freqText(ch(g, n).txFreq); };
        QCOMPARE(findPresetGroup("cb_fm")->channels.size(), 40);
        QCOMPARE(rx("cb_fm", "CB 01 FM"), QString("26.96500"));
        QCOMPARE(rx("cb_fm", "CB 09 FM"), QString("27.06500"));
        QCOMPARE(rx("cb_fm", "CB 19 FM"), QString("27.18500"));
        QCOMPARE(rx("cb_fm", "CB 23 FM"), QString("27.25500"));
        QCOMPARE(rx("cb_fm", "CB 24 FM"), QString("27.23500"));
        QCOMPARE(rx("cb_fm", "CB 25 FM"), QString("27.24500"));
        QCOMPARE(rx("cb_fm", "CB 40 FM"), QString("27.40500"));
        QCOMPARE(modeNames().at(ch("cb_am", "CB 09 AM").mode), QString("AM"));
        QCOMPARE(rx("cb_am", "CB 09 AM"), QString("27.06500"));

        QCOMPARE(findPresetGroup("marine")->channels.size(), 56);          // 1-28 and 60-88 without 70
        QCOMPARE(rx("marine", "Marine 16"), QString("156.80000"));
        QCOMPARE(tx("marine", "Marine 16"), QString("156.80000"));
        QCOMPARE(tx("marine", "Marine 01"), QString("156.05000"));
        QCOMPARE(rx("marine", "Marine 01"), QString("160.65000"));
        QCOMPARE(rx("marine", "Marine 06"), QString("156.30000"));
        QCOMPARE(tx("marine", "Marine 28"), QString("157.40000"));
        QCOMPARE(rx("marine", "Marine 28"), QString("162.00000"));
        QCOMPARE(tx("marine", "Marine 60"), QString("156.02500"));
        QCOMPARE(rx("marine", "Marine 60"), QString("160.62500"));
        QCOMPARE(rx("marine", "Marine 72"), QString("156.62500"));
        QCOMPARE(rx("marine", "Marine 88"), QString("157.42500"));
        QCOMPARE(tx("marine", "Marine 88"), QString("157.42500"));
        for (const Channel &c : findPresetGroup("marine")->channels) QVERIFY(c.name != "Marine 70");
        QCOMPARE(powerNames().at(ch("marine", "Marine 75").power), QString("Low"));

        QCOMPARE(findPresetGroup("pmr446")->channels.size(), 16);
        QCOMPARE(rx("pmr446", "PMR 01"), QString("446.00625"));
        QCOMPARE(rx("pmr446", "PMR 08"), QString("446.09375"));
        QCOMPARE(rx("pmr446", "PMR 16"), QString("446.19375"));
        QCOMPARE(bandwidthNames().at(ch("pmr446", "PMR 01").bandwidth), QString("Narrow"));
        QCOMPARE(findPresetGroup("lpd433")->channels.size(), 69);
        QCOMPARE(rx("lpd433", "LPD 01"), QString("433.07500"));
        QCOMPARE(rx("lpd433", "LPD 69"), QString("434.77500"));
        QCOMPARE(findPresetGroup("freenet")->channels.size(), 6);
        QCOMPARE(rx("freenet", "Freenet 1"), QString("149.02500"));
        QCOMPARE(rx("freenet", "Freenet 4"), QString("149.08750"));
        QCOMPARE(rx("freenet", "Freenet 6"), QString("149.11250"));
        QCOMPARE(rx("cb43", "43MHz 01"), QString("43.30000"));
        QCOMPARE(rx("cb43", "43MHz 24"), QString("43.58750"));
        QCOMPARE(rx("frs_gmrs", "FRS 01"), QString("462.56250"));
        QCOMPARE(rx("frs_gmrs", "FRS 08"), QString("467.56250"));
        QCOMPARE(rx("frs_gmrs", "FRS 15"), QString("462.55000"));
        QCOMPARE(rx("frs_gmrs", "FRS 22"), QString("462.72500"));
        QCOMPARE(rx("noaa", "NOAA WX1"), QString("162.55000"));
        QCOMPARE(rx("ham", "2m FM call"), QString("145.50000"));
    }
    void shippedListFilesAreClean() {
        QVERIFY2(presetProblems().isEmpty(), qPrintable(presetProblems().join("\n")));
        QVERIFY(presetGroups().size() >= 30);
        QCOMPARE(presetGroups().first().id, QString("cb_fm"));          // the number prefix sets the order
        for (const PresetGroup &g : presetGroups()) {                    // every list survives being written and read back
            const PresetFile again = parsePresetCsv(presetCsv(g).toUtf8(), g.id);
            QVERIFY2(again.problems.isEmpty(), qPrintable(g.id));
            QCOMPARE(again.group.name, g.name);
            QCOMPARE(again.group.note, g.note);
            QVERIFY2(again.group.channels == g.channels, qPrintable(g.id));
        }
        const PresetGroup *sat = findPresetGroup("satellites");
        QVERIFY(sat);
        for (const Channel &c : sat->channels) {                         // amateur satellite segments only
            for (quint32 f : {c.rxFreq, c.txFreq})
                QVERIFY2((f >= 14580000 && f <= 14600000) || (f >= 43500000 && f <= 43800000), qPrintable(c.name));
        }
    }
    void listFileFormat() {
        QCOMPARE(presetIdFromFileName("04-pmr446.csv"), QString("pmr446"));
        QCOMPARE(presetIdFromFileName("/x/My List.CSV"), QString("my list"));
        const PresetFile f = parsePresetCsv(
            "# name: Mine\n# note: my own\n# anything else is a comment\n\n"
            "Name;RX;TX;tx_tone;width;extra\n"
            "Home;145,500;;;narrow;x\n"
            "\"Rpt; north\";145.6;145.0;88.5;;\n"
            "DCS one;433.5;;D023N;;\n", "mine");
        QVERIFY2(f.problems.isEmpty(), qPrintable(f.problems.join("\n")));
        QCOMPARE(f.group.id, QString("mine"));
        QCOMPARE(f.group.name, QString("Mine"));
        QCOMPARE(f.group.note, QString("my own"));
        QCOMPARE(f.group.channels.size(), 3);
        const Channel a = f.group.channels.at(0), b = f.group.channels.at(1);
        QCOMPARE(a.name, QString("Home"));
        QCOMPARE(freqText(a.rxFreq), QString("145.50000"));
        QCOMPARE(a.txFreq, a.rxFreq);
        QCOMPARE(bandwidthNames().at(a.bandwidth), QString("Narrow"));
        QCOMPARE(modeNames().at(a.mode), QString("FM"));                 // defaults
        QCOMPARE(powerNames().at(a.power), QString("Low"));
        QCOMPARE(b.name, QString("Rpt; north"));
        QCOMPARE(freqText(b.txFreq), QString("145.00000"));
        QCOMPARE(toneText(b.txTone), QString("88.5"));
        QCOMPARE(bandwidthNames().at(b.bandwidth), QString("Wide"));
        QCOMPARE(toneText(f.group.channels.at(2).txTone), QString("D023N"));
    }
    void listFileProblemsAreReported() {
        const PresetFile f = parsePresetCsv(
            "name,rx,tx,tx_tone,mode\n"
            "Good,446.00625,,,\n"
            "Bad freq,abc,,,\n"
            "Out of range,5.0,,,\n"
            "Bad tx,145.5,zzz,,\n"
            "Bad tone,145.5,,xyz,\n"
            "Bad mode,145.5,,,CW\n"
            "A name that is far too long,145.5,,,\n", "x");
        QCOMPARE(f.group.channels.size(), 2);
        QCOMPARE(f.group.name, QString("x"));                            // no "# name:" line: the file name
        QCOMPARE(f.group.channels.at(1).name, QString("A name that is f"));
        QCOMPARE(f.problems.size(), 6);
        QVERIFY(f.problems.at(0).startsWith("line 3:"));
        QVERIFY(f.problems.at(4).contains("FM, AM, SSB"));
        QVERIFY(f.problems.at(5).contains("stored as"));
        QCOMPARE(parsePresetCsv("145.5,Home\n", "x").group.channels.size(), 0);     // no header row
        QVERIFY(!parsePresetCsv("145.5,Home\n", "x").problems.isEmpty());
        QVERIFY(!parsePresetCsv("", "x").problems.isEmpty());
        QVERIFY(!parsePresetCsv("# name: A list name that is too long\nname,rx\nA,145.5\n", "x").problems.isEmpty());
    }
    void userListsAddAndReplace() {
        QTemporaryDir dir;
        auto write = [&](const char *name, const char *text) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text);
        };
        write("pmr446.csv", "# name: My PMR\nname,rx\nOnly one,446.00625\n");
        write("99-club.csv", "# name: Club\nname,rx,tx\nClub rpt,145.6,145.0\nBroken,nope,\n");
        write("notes.txt", "not a list");
        const int before = int(presetGroups().size());
        int pmrAt = -1;
        for (int i = 0; i < before; ++i)
            if (presetGroups().at(i).id == "pmr446") pmrAt = i;
        QVERIFY(pmrAt >= 0);
        setPresetDirs({QStringLiteral(RT860_LISTS_SOURCE_DIR), dir.path(), dir.filePath("missing")});
        QCOMPARE(presetGroups().size(), before + 1);
        QCOMPARE(findPresetGroup("pmr446")->name, QString("My PMR"));   // replaced, same place in the order
        QCOMPARE(findPresetGroup("pmr446")->channels.size(), 1);
        QCOMPARE(presetGroups().at(pmrAt).id, QString("pmr446"));
        QCOMPARE(presetGroups().last().id, QString("club"));
        QCOMPARE(presetGroups().last().channels.size(), 1);
        QCOMPARE(presetProblems().size(), 1);
        QVERIFY(presetProblems().first().startsWith("99-club.csv: line 4:"));
        setPresetDirs({QStringLiteral(RT860_LISTS_SOURCE_DIR)});
        QCOMPARE(presetGroups().size(), before);
        QCOMPARE(findPresetGroup("pmr446")->channels.size(), 16);
    }
    void italianAirports() {
        int total = 0;
        for (const char *id : {"air_it_north", "air_it_centre", "air_it_south"}) {
            const PresetGroup *g = findPresetGroup(id);
            QVERIFY2(g, id);
            QVERIFY(presetUsesAirband(*g));
            total += int(g->channels.size());
            for (const Channel &c : g->channels) {
                QCOMPARE(modeNames().at(c.mode), QString("AM"));
                QVERIFY2(c.rxFreq >= 11800000 && c.rxFreq <= 13697500, qPrintable(c.name));
                QVERIFY2(c.name.startsWith("LI"), qPrintable(c.name));
            }
        }
        QVERIFY(total > 100);
        auto freq = [](const char *group, const QString &name) {
            for (const Channel &c : findPresetGroup(group)->channels)
                if (c.name == name) return freqText(c.rxFreq);
            return QString("missing");
        };
        QCOMPARE(freq("air_it_centre", "LIRF TWR"), QString("118.70000"));
        QCOMPARE(freq("air_it_north", "LIMC TWR"), QString("119.00000"));
        QCOMPARE(freq("air_it_north", "LIML TWR"), QString("118.10000"));
        QCOMPARE(freq("air_it_south", "LIRN TWR"), QString("118.50000"));
        QVERIFY(!presetUsesAirband(*findPresetGroup("pmr446")));
        QVERIFY(presetUsesAirband(*findPresetGroup("distress")));
    }
    void airbandCanBeMadeReceiveOnly() {
        QByteArray img = blankImage();
        QCOMPARE(makeAirbandReceiveOnly(img), 1);
        QCOMPARE(img.mid(133, 5), hex("0176008900"));                  // RX only, 118, 137
        QCOMPARE(settingValue(img, "lock2_type").toInt(), 0);
        QCOMPARE(makeAirbandReceiveOnly(img), 0);                         // already covered
        // ranges in use for something else are left alone
        img = blankImage();
        setSettingValue(img, "lock1_type", 2);
        setSettingValue(img, "lock1_start", 400);
        setSettingValue(img, "lock1_end", 480);
        QCOMPARE(makeAirbandReceiveOnly(img), 2);
        QCOMPARE(settingValue(img, "lock1_start").toInt(), 400);
        for (int n = 1; n <= 4; ++n) {
            setSettingValue(img, QStringLiteral("lock%1_type").arg(n), 2);
            setSettingValue(img, QStringLiteral("lock%1_start").arg(n), 400);
            setSettingValue(img, QStringLiteral("lock%1_end").arg(n), 480);
        }
        const QByteArray full = img;
        QCOMPARE(makeAirbandReceiveOnly(img), -1);
        QCOMPARE(img, full);
    }
    void italianRepeatersAreBuiltIn() {
        int lists = 0, total = 0;
        for (const PresetGroup &g : presetGroups()) {
            if (!g.id.startsWith("rpt_it_")) continue;
            ++lists;
            total += int(g.channels.size());
            QVERIFY2(g.name.startsWith("RPT "), qPrintable(g.name));
            for (const Channel &c : g.channels) {
                const bool vhf = c.rxFreq >= 14400000 && c.rxFreq <= 14600000, uhf = c.rxFreq >= 43000000 && c.rxFreq <= 44000000;
                QVERIFY2(vhf || uhf, qPrintable(c.name));
                QVERIFY2(c.txFreq != c.rxFreq, qPrintable(c.name));
                // the input stays inside the same amateur band (a few repeaters have unusual shifts)
                QVERIFY2(vhf ? c.txFreq >= 14400000 && c.txFreq <= 14600000 : c.txFreq >= 43000000 && c.txFreq <= 44000000, qPrintable(c.name));
                QCOMPARE(modeNames().at(c.mode), QString("FM"));
                QCOMPARE(c.rxTone, quint16(0));
            }
        }
        QVERIFY(lists >= 15);
        QVERIFY(total > 300);
        QVERIFY(findPresetGroup("rpt_it_lazio"));
        QVERIFY(findPresetGroup("rpt_it_trentino_aa"));
        bool frassinetto = false;
        for (const Channel &c : findPresetGroup("rpt_it_piemonte")->channels)
            frassinetto = frassinetto || (c.name == "R0 Frassinetto" && freqText(c.rxFreq) == "145.60000" && freqText(c.txFreq) == "145.00000" && toneText(c.txTone) == "127.3");
        QVERIFY(frassinetto);
    }
    void importFillsEmptySlotsInOrderAndMakesAZone() {
        QByteArray img = sampleImage();                                  // slots 1, 2 and 1001 in use, zone 1 in use
        const PresetGroup &g = *findPresetGroup("freenet");
        const ImportResult r = importPreset(img, g, 0, true);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.placed, (QList<int>{2, 3, 4, 5, 6, 7}));
        QCOMPARE(decodeChannel(img, 0).name, QString("RPT VHF"));        // existing channels untouched
        QCOMPARE(decodeChannel(img, 2).name, QString("Freenet 1"));
        QCOMPARE(decodeChannel(img, 7).name, QString("Freenet 6"));
        QCOMPARE(r.zone, 1);
        QCOMPARE(decodeZone(img, 0).name, QString("Home"));
        QCOMPARE(decodeZone(img, 1), (Zone{"Freenet", {2, 3, 4, 5, 6, 7}}));
        // a start slot in the middle skips used slots
        const ImportResult r2 = importPreset(img, g, 5, false);
        QVERIFY(r2.ok);
        QCOMPARE(r2.placed, (QList<int>{8, 9, 10, 11, 12, 13}));
        QCOMPARE(r2.zone, -1);
    }
    void factoryNamedZonesAreFree() {
        // a real radio names every zone "Zone-001".."Zone-256"
        QByteArray img = blankImage();
        for (int z = 0; z < ZoneCount; ++z)
            encodeZone(img, z, Zone{QStringLiteral("Zone-%1").arg(z + 1, 3, 10, QLatin1Char('0')), {}});
        encodeZone(img, 0, Zone{"Zone-001", {0, 1}});      // has channels: in use
        encodeZone(img, 1, Zone{"Zone-005", {}});          // renamed: in use
        QCOMPARE(freeZones(img), ZoneCount - 2);
        const ImportResult r = importPreset(img, *findPresetGroup("cb_fm"), 0, true);
        QVERIFY(r.ok);
        QCOMPARE(r.zone, 2);
        QCOMPARE(decodeZone(img, 2).channels.size(), 40);
        QCOMPARE(decodeZone(img, 1).name, QString("Zone-005"));
    }
    void importIsAllOrNothing() {
        QByteArray img = sampleImage();
        const QByteArray before = img;
        QCOMPARE(freeSlotsFrom(img, 0), 1021);
        QCOMPARE(freeSlotsFrom(img, 1000), 23);
        QCOMPARE(freeZones(img), ZoneCount - 1);
        QCOMPARE(freeZones(blankImage()), ZoneCount);
        const ImportResult r = importPreset(img, *findPresetGroup("cb_fm"), 1000, true);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("40"));
        QCOMPARE(img, before);
        QVERIFY(!importPreset(img, *findPresetGroup("cb_fm"), 5000, false).ok);
        // no unused zone left
        for (int z = 0; z < ZoneCount; ++z) encodeZone(img, z, Zone{"Z", {}});
        const QByteArray full = img;
        QVERIFY(!importPreset(img, *findPresetGroup("freenet"), 0, true).ok);
        QCOMPARE(img, full);
        QVERIFY(importPreset(img, *findPresetGroup("freenet"), 0, false).ok);
    }

    // ---- reading
    void readsTheWholeRadio() {
        FakeRadio fake;
        fake.image = sampleImage();
        fake.image.replace(TimeOffset, 4, "junk");                 // not in the radio: never transferred
        Radio radio(fake);
        radio.connect();
        int calls = 0, last = 0;
        const QByteArray img = radio.readImage([&](int d, int t) { ++calls; last = d; QCOMPARE(t, 184); });
        radio.end();
        QByteArray expected = fake.image;
        expected.replace(TimeOffset, 4, QByteArray(4, '\0'));
        QCOMPARE(img, expected);
        QCOMPARE(calls, 184);
        QCOMPARE(last, 184);
        QCOMPARE(fake.ends, 1);
        QCOMPARE(decodeChannel(img, 1000).name, QString("FAR"));
    }
    void connectRetriesAndSkipsZeroBytes() {
        FakeRadio fake;
        fake.ignoreConnects = 2;
        Radio radio(fake);
        radio.connect(3, 20);
        QVERIFY(fake.connected);
    }
    void silentRadioIsAnError() {
        FakeRadio fake;
        fake.ignoreConnects = 99;
        Radio radio(fake);
        QVERIFY_EXCEPTION_THROWN(radio.connect(3, 20), RadioError);
    }
    void damagedReplyIsAnError() {
        FakeRadio fake;
        fake.corruptReadReply = 5;
        Radio radio(fake, 200);
        radio.connect();
        QVERIFY_EXCEPTION_THROWN(radio.readImage(), RadioError);
    }
    void radioGoingSilentMidReadIsAnError() {
        FakeRadio fake;
        fake.silentAfterReads = 10;
        Radio radio(fake, 100);
        radio.connect();
        QVERIFY_EXCEPTION_THROWN(radio.readImage(), RadioError);
    }

    // ---- writing
    void planListsOnlyChangedBlocksAndIgnoresThePassword() {
        const QByteArray base = sampleImage();
        QByteArray work = base;
        QVERIFY(planWrites(base, work).isEmpty());
        work.replace(CfgOffset, PasswordLength, QByteArray(12, 'x'));
        work.replace(TimeOffset, 4, "time");
        QVERIFY(planWrites(base, work).isEmpty());
        Channel c = decodeChannel(work, 1000);
        c.name = "CHANGED";
        encodeChannel(work, 1000, c);                              // 1000 * 48 = block 46
        setSettingValue(work, "squelch", 9);
        encodeZone(work, 255, Zone{"Last", {3}});                  // 255 * 512 = block 127
        QCOMPARE(planWrites(base, work), (QList<BlockRef>{{Cfg, 0}, {Channels, 46}, {Zones, 127}}));
    }
    void writesOnlyWhatChangedThenVerifies() {
        QTemporaryDir dir;
        FakeRadio fake;
        fake.image = sampleImage();
        const QByteArray base = fake.image;
        QByteArray work = base;
        Channel c = decodeChannel(work, 1);
        c.name = "EDITED";
        encodeChannel(work, 1, c);
        QStringList stages;
        WriteOptions o = fastOptions(dir);
        o.progress = [&](const QString &s, int, int) { if (stages.isEmpty() || stages.last() != s) stages << s; };
        const WriteResult r = writeToRadio(fake, base, work, o);
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(fake.writes, (QList<QPair<int, int>>{{0x90, 0}, {0x91, 0}}));   // settings block 0 always first
        QCOMPARE(r.blocksWritten, 2);
        QCOMPARE(decodeChannel(fake.image, 1).name, QString("EDITED"));
        QCOMPARE(fake.image, work);
        QCOMPARE(r.radioImage, work);
        QCOMPARE(fake.sessions, 2);                                // write, then reconnect to verify
        QCOMPARE(fake.ends, 2);
        QCOMPARE(stages, (QStringList{"Reading the radio", "Writing", "Verifying"}));
        // the backup holds the radio as it was
        QFile f(r.backupPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), base);
        QVERIFY(r.backupPath.endsWith(".88dat"));
    }
    void nothingToWrite() {
        QTemporaryDir dir;
        FakeRadio fake;
        fake.image = sampleImage();
        const WriteResult r = writeToRadio(fake, fake.image, fake.image, fastOptions(dir));
        QCOMPARE(r.status, WriteResult::NothingToWrite);
        QVERIFY(fake.writes.isEmpty());
        QCOMPARE(fake.sessions, 0);
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }
    void passwordAlwaysComesFromTheRadio() {
        QTemporaryDir dir;
        FakeRadio fake;
        fake.image = sampleImage();
        QByteArray work = blankImage();                            // as if opened from a file: FF password
        setSettingValue(work, "squelch", 2);
        const WriteResult r = writeToRadio(fake, {}, work, fastOptions(dir));
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(fake.image.left(PasswordLength), QByteArray("SECRETSECRET"));
        QCOMPARE(fake.image.mid(PasswordLength), work.mid(PasswordLength));
        QVERIFY(decodeChannel(fake.image, 0).empty);               // the file had no channels
    }
    void fileImageEqualToRadioWritesNothing() {
        QTemporaryDir dir;
        FakeRadio fake;
        fake.image = sampleImage();
        const WriteResult r = writeToRadio(fake, {}, fake.image, fastOptions(dir));
        QCOMPARE(r.status, WriteResult::NothingToWrite);
        QVERIFY(fake.writes.isEmpty());
        QCOMPARE(fake.ends, 1);
    }
    void conflictStopsBeforeWriting() {
        QTemporaryDir dir;
        FakeRadio fake;
        fake.image = sampleImage();
        const QByteArray base = fake.image;
        QByteArray work = base;
        Channel c = decodeChannel(work, 1);
        c.name = "MINE";
        encodeChannel(work, 1, c);
        c.name = "RADIO";                                          // changed on the radio meanwhile
        encodeChannel(fake.image, 1, c);
        WriteResult r = writeToRadio(fake, base, work, fastOptions(dir));
        QCOMPARE(r.status, WriteResult::Conflict);
        QCOMPARE(r.conflicts, (QList<BlockRef>{{Channels, 0}}));
        QVERIFY(fake.writes.isEmpty());
        QCOMPARE(fake.ends, 1);
        QCOMPARE(decodeChannel(r.radioImage, 1).name, QString("RADIO"));
        WriteOptions o = fastOptions(dir);
        o.overwriteConflicts = true;
        r = writeToRadio(fake, base, work, o);
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(decodeChannel(fake.image, 1).name, QString("MINE"));
    }
    void changeElsewhereOnTheRadioIsNotAConflictAndIsKept() {
        QTemporaryDir dir;
        FakeRadio fake;
        fake.image = sampleImage();
        const QByteArray base = fake.image;
        QByteArray work = base;
        Channel c = decodeChannel(work, 1);
        c.name = "MINE";
        encodeChannel(work, 1, c);
        encodeZone(fake.image, 9, Zone{"OnRadio", {0}});           // different block
        const WriteResult r = writeToRadio(fake, base, work, fastOptions(dir));
        QCOMPARE(r.status, WriteResult::Ok);
        QCOMPARE(decodeZone(fake.image, 9).name, QString("OnRadio"));
        QCOMPARE(decodeChannel(fake.image, 1).name, QString("MINE"));
    }
    void verifyFailureIsReported() {
        QTemporaryDir dir;
        FakeRadio fake;
        fake.image = sampleImage();
        const QByteArray base = fake.image;
        QByteArray work = base;
        encodeZone(work, 0, Zone{"Renamed", {0}});
        fake.dropWrites = true;
        const WriteResult r = writeToRadio(fake, base, work, fastOptions(dir));
        QCOMPARE(r.status, WriteResult::VerifyFailed);
        QCOMPARE(r.failed, (QList<BlockRef>{{Zones, 0}}));
        QCOMPARE(r.radioImage, base);
    }
    void backupFolderProblemStopsBeforeWriting() {
        QTemporaryDir dir;
        QFile blocker(dir.path() + "/file");
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        FakeRadio fake;
        fake.image = sampleImage();
        QByteArray work = fake.image;
        setSettingValue(work, "squelch", 1);
        WriteOptions o = fastOptions(dir);
        o.backupDir = dir.path() + "/file/sub";
        QVERIFY_EXCEPTION_THROWN(writeToRadio(fake, fake.image, work, o), RadioError);
        QVERIFY(fake.writes.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"

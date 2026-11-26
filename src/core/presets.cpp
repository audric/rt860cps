#include "presets.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>

namespace rt860 {

namespace {

QStringList g_dirs, g_problems;
QList<PresetGroup> g_groups;

QString csvField(const QString &v) {
    if (!v.contains(',') && !v.contains('"') && !v.startsWith('#') && v == v.trimmed()) return v;
    QString q = v;
    return '"' + q.replace('"', "\"\"") + '"';
}

QStringList csvSplit(const QString &line, QChar sep) {
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line.at(i + 1) == '"') { field += '"'; ++i; }
            else if (c == '"') quoted = false;
            else field += c;
        } else if (c == '"') quoted = true;
        else if (c == sep) { fields << field.trimmed(); field.clear(); }
        else field += c;
    }
    fields << field.trimmed();
    return fields;
}

// Index of `text` in `names`, ignoring case; -1 if not there.
int nameIndex(const QStringList &names, const QString &text) {
    for (int i = 0; i < names.size(); ++i)
        if (names.at(i).compare(text, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

void loadPresets() {
    g_groups.clear();
    g_problems.clear();
    for (const QString &dirPath : g_dirs) {
        const QDir dir(dirPath);
        for (const QString &fileName : dir.entryList({QStringLiteral("*.csv")}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
            QFile f(dir.filePath(fileName));
            if (!f.open(QIODevice::ReadOnly)) {
                g_problems << QStringLiteral("%1: %2").arg(fileName, f.errorString());
                continue;
            }
            const PresetFile parsed = parsePresetCsv(f.readAll(), presetIdFromFileName(fileName));
            for (const QString &p : parsed.problems) g_problems << QStringLiteral("%1: %2").arg(fileName, p);
            if (parsed.group.channels.isEmpty()) continue;
            bool replaced = false;
            for (PresetGroup &g : g_groups)
                if (g.id == parsed.group.id) {
                    g = parsed.group;
                    replaced = true;
                }
            if (!replaced) g_groups << parsed.group;
        }
    }
}

} // namespace

QString presetIdFromFileName(const QString &fileName) {
    static const QRegularExpression order(QStringLiteral("^[0-9]+[-_ ]"));
    QString id = QFileInfo(fileName).completeBaseName();
    return id.remove(order).toLower();
}

QString presetCsv(const PresetGroup &group) {
    QString out = QStringLiteral("# name: %1\n# note: %2\nname,rx,tx,tx_tone,rx_tone,mode,width,power\n").arg(group.name, group.note);
    for (const Channel &c : group.channels)
        out += QStringList{csvField(c.name), freqText(c.rxFreq), c.txFreq == c.rxFreq ? QString() : freqText(c.txFreq), toneText(c.txTone), toneText(c.rxTone),
                           modeNames().at(c.mode), bandwidthNames().at(c.bandwidth), powerNames().at(c.power)}.join(',') + '\n';
    return out;
}

PresetFile parsePresetCsv(const QByteArray &bytes, const QString &id) {
    PresetFile result;
    PresetGroup &g = result.group;
    g.id = id;
    g.name = id;
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    if (utf8.hasError()) text = QString::fromLatin1(bytes);
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);

    QStringList columns;
    QChar sep = ',';
    QByteArray scratch = blankImage();
    QStringList names;
    int lineNo = 0;
    for (const QString &rawLine : text.split('\n')) {
        ++lineNo;
        const QString line = rawLine.trimmed();
        auto problem = [&](const QString &what) { result.problems << QStringLiteral("line %1: %2").arg(lineNo).arg(what); };
        if (line.isEmpty()) continue;
        if (line.startsWith('#')) {
            const QString body = line.mid(1).trimmed();
            if (body.startsWith(QLatin1String("name:"), Qt::CaseInsensitive)) g.name = body.mid(5).trimmed();
            else if (body.startsWith(QLatin1String("note:"), Qt::CaseInsensitive)) g.note = body.mid(5).trimmed();
            continue;
        }
        if (columns.isEmpty()) {
            sep = line.count(';') > line.count(',') ? QChar(';') : QChar(',');
            for (const QString &c : csvSplit(line, sep)) columns << c.toLower();
            if (!columns.contains(QLatin1String("name")) || !columns.contains(QLatin1String("rx"))) {
                problem(QStringLiteral("the first row must name the columns and include \"name\" and \"rx\""));
                return result;
            }
            continue;
        }
        const QStringList fields = csvSplit(line, sep);
        auto value = [&](const char *column) {
            const qsizetype i = columns.indexOf(QLatin1String(column));
            return i >= 0 && i < fields.size() ? fields.at(i) : QString();
        };
        Channel c;
        c.empty = false;
        c.power = 0;
        c.name = value("name");
        if (!parseFreq(value("rx").replace(',', '.'), c.rxFreq)) {
            problem(QStringLiteral("\"%1\" is not a frequency in MHz the radio covers; row left out").arg(value("rx")));
            continue;
        }
        c.txFreq = c.rxFreq;
        if (!value("tx").isEmpty() && !parseFreq(value("tx").replace(',', '.'), c.txFreq)) {
            problem(QStringLiteral("\"%1\" is not a frequency in MHz the radio covers; row left out").arg(value("tx")));
            continue;
        }
        bool ok = parseTone(value("tx_tone"), c.txTone) && parseTone(value("rx_tone"), c.rxTone);
        if (!ok) {
            problem(QStringLiteral("tone not understood (use 88.5, D023N or D023I); row left out"));
            continue;
        }
        const struct { const char *column; const QStringList &names; int *target; } lists[] = {
            {"mode", modeNames(), &c.mode}, {"width", bandwidthNames(), &c.bandwidth}, {"power", powerNames(), &c.power}};
        for (const auto &l : lists) {
            const QString v = value(l.column);
            if (v.isEmpty()) continue;
            const int i = nameIndex(l.names, v);
            if (i < 0) {
                problem(QStringLiteral("%1 \"%2\" is not one of %3; row left out").arg(QLatin1String(l.column), v, l.names.join(QStringLiteral(", "))));
                ok = false;
            } else {
                *l.target = i;
            }
        }
        if (!ok) continue;
        // what the radio can actually store: names are cut to 16 bytes
        encodeChannel(scratch, 0, c);
        const Channel stored = decodeChannel(scratch, 0);
        if (stored.name != c.name) problem(QStringLiteral("name \"%1\" stored as \"%2\"").arg(c.name, stored.name));
        if (!channelProblems(scratch, 0).isEmpty()) {
            problem(channelProblems(scratch, 0).join(QStringLiteral("; ")) + QStringLiteral("; row left out"));
            continue;
        }
        g.channels << stored;
    }
    if (columns.isEmpty()) result.problems << QStringLiteral("no rows");
    const QString zoneName = decodeName(encodeName(g.name, 16));
    if (zoneName != g.name) {
        result.problems << QStringLiteral("list name \"%1\" cut to \"%2\" (16 characters at most)").arg(g.name, zoneName);
        g.name = zoneName;
    }
    return result;
}

void setPresetDirs(const QStringList &dirs) {
    g_dirs = dirs;
    loadPresets();
}
QStringList presetDirs() { return g_dirs; }
QStringList presetProblems() { return g_problems; }
const QList<PresetGroup> &presetGroups() { return g_groups; }

bool presetUsesAirband(const PresetGroup &group) {
    for (const Channel &c : group.channels)
        if (c.txFreq >= 11800000 && c.txFreq <= 13700000) return true;
    return false;
}

int makeAirbandReceiveOnly(QByteArray &image) {
    int free = 0;
    for (int n = 1; n <= 4; ++n) {
        const QString p = QStringLiteral("lock%1_").arg(n);
        const int type = settingValue(image, p + "type").toInt();
        if (type != 0 && settingValue(image, p + "start").toInt() <= 118 && settingValue(image, p + "end").toInt() >= 137) return 0;
        if (type == 0 && !free) free = n;
    }
    if (!free) return -1;
    const QString p = QStringLiteral("lock%1_").arg(free);
    setSettingValue(image, p + "type", 1);       // RX only
    setSettingValue(image, p + "start", 118);
    setSettingValue(image, p + "end", 137);
    return free;
}

const PresetGroup *findPresetGroup(const QString &id) {
    for (const PresetGroup &g : presetGroups())
        if (g.id == id) return &g;
    return nullptr;
}

int freeSlotsFrom(const QByteArray &image, int firstSlot) {
    int n = 0;
    for (int i = qMax(0, firstSlot); i < ChannelCount; ++i) n += decodeChannel(image, i).empty;
    return n;
}

bool zoneIsFree(const QByteArray &image, int index) {
    const Zone zone = decodeZone(image, index);
    if (!zone.channels.isEmpty()) return false;
    // The radio names every zone "Zone-001".."Zone-256" from the factory.
    return zone.name.isEmpty() || zone.name == QStringLiteral("Zone-%1").arg(index + 1, 3, 10, QLatin1Char('0'));
}

int freeZones(const QByteArray &image) {
    int n = 0;
    for (int z = 0; z < ZoneCount; ++z) n += zoneIsFree(image, z);
    return n;
}

ImportResult importPreset(QByteArray &image, const PresetGroup &group, int firstSlot, bool makeZone) {
    ImportResult r;
    if (image.size() != ImageSize || firstSlot < 0 || firstSlot >= ChannelCount) {
        r.error = QStringLiteral("Bad starting slot.");
        return r;
    }
    QList<int> placed;
    for (int i = firstSlot; i < ChannelCount && placed.size() < group.channels.size(); ++i)
        if (decodeChannel(image, i).empty) placed << i;
    if (placed.size() < group.channels.size()) {
        r.error = QStringLiteral("\"%1\" needs %2 empty slots but only %3 are free from slot %4 on.")
                      .arg(group.name).arg(group.channels.size()).arg(placed.size()).arg(firstSlot + 1);
        return r;
    }
    int zone = -1;
    if (makeZone) {
        for (int z = 0; z < ZoneCount && zone < 0; ++z)
            if (zoneIsFree(image, z)) zone = z;
        if (zone < 0) {
            r.error = QStringLiteral("There is no unused zone left for \"%1\".").arg(group.name);
            return r;
        }
    }
    for (int i = 0; i < placed.size(); ++i) encodeChannel(image, placed.at(i), group.channels.at(i));
    if (zone >= 0) {
        Zone z{group.name, {}};
        for (int s : placed.mid(0, ZoneSlots)) z.channels << quint16(s);
        encodeZone(image, zone, z);
    }
    r.ok = true;
    r.placed = placed;
    r.zone = zone;
    return r;
}

} // namespace rt860

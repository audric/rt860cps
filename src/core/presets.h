// Ready-made channel lists (CB, marine VHF, PMR446 ...) that can be added to an image.
// Each list is a CSV file in a lists folder; see lists/README.md for the format.
#pragma once
#include "layout.h"

namespace rt860 {

struct PresetGroup {
    QString id;
    QString name;           // also used as the zone name; at most 16 characters
    QString note;           // one line: where it applies, what to know
    QList<Channel> channels;
};
// One list as CSV text, and back. `id` is the file name without ".csv" and
// without a leading "NN-" (which only sets the order).
QString presetCsv(const PresetGroup &group);
struct PresetFile {
    PresetGroup group;
    QStringList problems;    // "line 7: ..." for rows that were left out or changed
};
PresetFile parsePresetCsv(const QByteArray &text, const QString &id);
QString presetIdFromFileName(const QString &fileName);

// Folders the lists are read from, in order. A file in a later folder replaces
// the list with the same id from an earlier one; other files add lists.
void setPresetDirs(const QStringList &dirs);    // also reloads
QStringList presetDirs();
QStringList presetProblems();                   // from the last load, "file: line N: ..."
const QList<PresetGroup> &presetGroups();
const PresetGroup *findPresetGroup(const QString &id);

// True if the group has channels in the aeronautical band (118-137 MHz), where
// transmitting is never appropriate from this radio.
bool presetUsesAirband(const PresetGroup &group);
// Sets a lock range to "RX only" for 118-137 MHz. Returns the range used (1-4),
// 0 if a range already covers the band, or -1 (image untouched) if all four
// lock ranges are in use for something else.
int makeAirbandReceiveOnly(QByteArray &image);

// Empty channel slots at or after firstSlot.
int freeSlotsFrom(const QByteArray &image, int firstSlot);
// A zone an import can use: no channels, and no name or still the factory name
// ("Zone-001" for the first zone, and so on).
bool zoneIsFree(const QByteArray &image, int index);
int freeZones(const QByteArray &image);

struct ImportResult {
    bool ok = false;
    QString error;
    QList<int> placed;       // where the channels went, in order
    int zone = -1;          // zone created, or -1
};
// Puts the group's channels into the empty slots at or after firstSlot, in
// order, skipping slots that are in use. With makeZone, also fills the first
// unused zone with them. Changes nothing unless everything fits.
ImportResult importPreset(QByteArray &image, const PresetGroup &group, int firstSlot, bool makeZone);

} // namespace rt860

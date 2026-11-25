#include "channelmodel.h"
#include <QApplication>
#include <QBrush>
#include <QComboBox>
#include <QPalette>
#include <QStyle>

using rt860::Channel;

namespace {

const char *kHeaders[ChannelModel::ColumnCount] = {"#", "Name", "RX frequency", "TX frequency", "RX tone", "TX tone", "Mode",
                                                  "Width", "Power", "Scan", "Busy lock", "TOT", "Tone mode", "Mute code",
                                                  "Scrambler", "Tail tone"};
const char *kHeaderTips[ChannelModel::ColumnCount] = {
    "Memory slot", "Up to 16 characters (8 Chinese characters)", "Receive frequency in MHz. Clear it to delete the channel.",
    "Transmit frequency in MHz", "Tone needed to open the squelch", "Tone sent when transmitting", "Modulation",
    "Channel width", "Transmit power", "Include this channel when scanning", "Do not transmit while the channel is busy",
    "Transmit time limit", "How the tones are used (Normal, the radio's Enc 1-3, or Mute code)",
    "Mute code, 8 hexadecimal digits", "Voice scrambler", "Squelch tail elimination"};

QString at(const QStringList &l, int i) { return i >= 0 && i < l.size() ? l.at(i) : QStringLiteral("?%1").arg(i); }

QStringList options(int col) {
    switch (col) {
    case ChannelModel::RxTone:
    case ChannelModel::TxTone: return rt860::allToneTexts();
    case ChannelModel::Mode: return rt860::modeNames();
    case ChannelModel::Width: return rt860::bandwidthNames();
    case ChannelModel::Power: return rt860::powerNames();
    case ChannelModel::BusyLock: return rt860::busyLockNames();
    case ChannelModel::Tot: return rt860::timeNames().mid(0, 32);   // the field has 5 bits
    case ChannelModel::ToneMode: return rt860::toneModeNames();
    case ChannelModel::Scrambler: return rt860::scramblerNames();
    case ChannelModel::TailTone: return rt860::tailToneNames();
    }
    return {};
}

QString text(const Channel &c, int col) {
    switch (col) {
    case ChannelModel::Name: return c.name;
    case ChannelModel::RxFreq: return rt860::freqText(c.rxFreq);
    case ChannelModel::TxFreq: return rt860::freqText(c.txFreq);
    case ChannelModel::RxTone: return rt860::toneValid(c.rxTone) ? rt860::toneText(c.rxTone) : QStringLiteral("?");
    case ChannelModel::TxTone: return rt860::toneValid(c.txTone) ? rt860::toneText(c.txTone) : QStringLiteral("?");
    case ChannelModel::Mode: return at(rt860::modeNames(), c.mode);
    case ChannelModel::Width: return at(rt860::bandwidthNames(), c.bandwidth);
    case ChannelModel::Power: return at(rt860::powerNames(), c.power);
    case ChannelModel::BusyLock: return at(rt860::busyLockNames(), c.busyLock);
    case ChannelModel::Tot: return at(rt860::timeNames(), c.tot);
    case ChannelModel::ToneMode: return at(rt860::toneModeNames(), c.toneMode);
    case ChannelModel::MuteCode: return QStringLiteral("%1").arg(c.muteCode, 8, 16, QLatin1Char('0')).toUpper();
    case ChannelModel::Scrambler: return at(rt860::scramblerNames(), c.scrambler);
    case ChannelModel::TailTone: return at(rt860::tailToneNames(), c.tailTone);
    }
    return {};
}

} // namespace

ChannelModel::ChannelModel(QObject *parent) : QAbstractTableModel(parent) {}

void ChannelModel::setImages(QByteArray *work, const QByteArray *base) {
    beginResetModel();
    m_work = work;
    m_base = base;
    endResetModel();
}

void ChannelModel::reload() {
    beginResetModel();
    endResetModel();
}

int ChannelModel::rowCount(const QModelIndex &parent) const { return parent.isValid() || !ready() ? 0 : rt860::ChannelCount; }
int ChannelModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

Channel ChannelModel::channel(int row) const { return ready() ? rt860::decodeChannel(*m_work, row) : Channel{}; }

QVariant ChannelModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};
    if (role == Qt::DisplayRole) return QString::fromLatin1(kHeaders[section]);
    if (role == Qt::ToolTipRole) return QString::fromLatin1(kHeaderTips[section]);
    return {};
}

QVariant ChannelModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || !ready()) return {};
    const int row = index.row(), col = index.column();
    const Channel c = rt860::decodeChannel(*m_work, row);

    if (role == EmptyRole) return c.empty;
    if (role == OptionsRole) return options(col);
    if (role == FreeTextRole) return col == RxTone || col == TxTone;
    if (col == Num) {
        const QStringList problems = rt860::channelProblems(*m_work, row);
        if (role == Qt::DisplayRole) return row + 1;
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignRight | Qt::AlignVCenter);
        if (role == Qt::ToolTipRole && !problems.isEmpty()) return problems.join('\n');
        if (role == Qt::DecorationRole && !problems.isEmpty())
            return QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
        if (role == Qt::ForegroundRole && c.empty) return QApplication::palette().brush(QPalette::Disabled, QPalette::Text);
        return {};
    }
    if (c.empty) {
        if (role == Qt::EditRole && (col == RxFreq || col == Name)) return QString();
        return {};
    }
    if (col == Scan) {
        if (role == Qt::CheckStateRole) return c.scanSkip ? Qt::Unchecked : Qt::Checked;
    } else if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return text(c, col);
    }
    if (role == Qt::TextAlignmentRole && (col == RxFreq || col == TxFreq)) return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::FontRole && m_base && m_base->size() == rt860::ImageSize) {
        // changed since the image was read: shown in bold
        const Channel b = rt860::decodeChannel(*m_base, row);
        const bool changed = b.empty || (col == Scan ? b.scanSkip != c.scanSkip : text(b, col) != text(c, col));
        if (changed) {
            QFont f = QApplication::font();
            f.setBold(true);
            return f;
        }
    }
    return {};
}

Qt::ItemFlags ChannelModel::flags(const QModelIndex &index) const {
    if (!index.isValid() || !ready()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    const int col = index.column();
    if (col == Num) return f;
    const bool empty = rt860::decodeChannel(*m_work, index.row()).empty;
    if (empty) return col == RxFreq ? f | Qt::ItemIsEditable : f;
    return col == Scan ? f | Qt::ItemIsUserCheckable : f | Qt::ItemIsEditable;
}

bool ChannelModel::fail(const QString &reason) {
    emit rejected(reason);
    return false;
}

bool ChannelModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || !ready()) return false;
    const int row = index.row(), col = index.column();
    Channel c = rt860::decodeChannel(*m_work, row);
    const Channel before = c;

    if (col == Scan) {
        if (role != Qt::CheckStateRole || c.empty) return false;
        c.scanSkip = value.toInt() != Qt::Checked;
    } else {
        if (role != Qt::EditRole) return false;
        const QString t = value.toString().trimmed();
        if (col == RxFreq) {
            if (t.isEmpty()) {
                c = Channel{};
            } else {
                quint32 f = 0;
                if (!rt860::parseFreq(t, f)) return fail(tr("\"%1\" is not a frequency between 18 and 1000 MHz.").arg(t));
                if (c.empty) {
                    c = Channel{};
                    c.empty = false;
                    c.txFreq = f;
                } else if (c.txFreq == c.rxFreq) {
                    c.txFreq = f;      // simplex stays simplex
                }
                c.rxFreq = f;
            }
        } else if (c.empty) {
            return false;
        } else if (col == TxFreq) {
            quint32 f = 0;
            if (!rt860::parseFreq(t, f)) return fail(tr("\"%1\" is not a frequency between 18 and 1000 MHz.").arg(t));
            c.txFreq = f;
        } else if (col == Name) {
            if (rt860::decodeName(rt860::encodeName(t, 16)) != t)
                return fail(tr("Names can have at most 16 characters (Chinese characters count as two) and must exist in GBK."));
            c.name = t;
        } else if (col == RxTone || col == TxTone) {
            quint16 tone = 0;
            if (!rt860::parseTone(t, tone)) return fail(tr("\"%1\" is not a tone. Use for example 88.5, D023N or D023I.").arg(t));
            (col == RxTone ? c.rxTone : c.txTone) = tone;
        } else if (col == MuteCode) {
            bool ok = false;
            const quint32 v = t.toUInt(&ok, 16);
            if (!ok || t.size() > 8) return fail(tr("The mute code is up to 8 hexadecimal digits."));
            c.muteCode = v;
        } else {
            const int i = options(col).indexOf(t);
            if (i < 0) return fail(tr("\"%1\" is not one of the choices for this column.").arg(t));
            switch (col) {
            case Mode: c.mode = i; break;
            case Width: c.bandwidth = i; break;
            case Power: c.power = i; break;
            case BusyLock: c.busyLock = i; break;
            case Tot: c.tot = i; break;
            case ToneMode: c.toneMode = i; break;
            case Scrambler: c.scrambler = i; break;
            case TailTone: c.tailTone = i; break;
            }
        }
    }
    if (c == before) return true;
    setChannel(row, c);
    return true;
}

void ChannelModel::setChannel(int row, const Channel &c) {
    if (!ready() || row < 0 || row >= rt860::ChannelCount) return;
    rt860::encodeChannel(*m_work, row, c);
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    emit edited();
}

void ChannelModel::swapChannels(int a, int b) {
    if (!ready() || a == b) return;
    // Swap the raw records so bytes this program does not understand move too.
    const int oa = rt860::ChannelsOffset + a * rt860::ChannelSize, ob = rt860::ChannelsOffset + b * rt860::ChannelSize;
    const QByteArray ra = m_work->mid(oa, rt860::ChannelSize), rb = m_work->mid(ob, rt860::ChannelSize);
    m_work->replace(oa, rt860::ChannelSize, rb);
    m_work->replace(ob, rt860::ChannelSize, ra);
    for (int z = 0; z < rt860::ZoneCount; ++z) {
        rt860::Zone zone = rt860::decodeZone(*m_work, z);
        bool touched = false;
        for (quint16 &ch : zone.channels) {
            if (ch == a) { ch = quint16(b); touched = true; }
            else if (ch == b) { ch = quint16(a); touched = true; }
        }
        if (touched) rt860::encodeZone(*m_work, z, zone);
    }
    emit dataChanged(index(qMin(a, b), 0), index(qMax(a, b), ColumnCount - 1));
    emit edited();
}

int ChannelModel::problemCount() const {
    int n = 0;
    if (ready())
        for (int i = 0; i < rt860::ChannelCount; ++i) n += !rt860::channelProblems(*m_work, i).isEmpty();
    return n;
}

bool ChannelFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    const QAbstractItemModel *m = sourceModel();
    const bool empty = m->data(m->index(row, ChannelModel::Num, parent), ChannelModel::EmptyRole).toBool();
    if (m_hideEmpty && empty) return false;
    if (m_text.isEmpty()) return true;
    if (empty) return false;
    for (int col : {int(ChannelModel::Num), int(ChannelModel::Name), int(ChannelModel::RxFreq), int(ChannelModel::TxFreq)})
        if (m->data(m->index(row, col, parent), Qt::DisplayRole).toString().contains(m_text, Qt::CaseInsensitive)) return true;
    return false;
}

QWidget *ChannelDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const QStringList choices = index.data(ChannelModel::OptionsRole).toStringList();
    if (choices.isEmpty()) return QStyledItemDelegate::createEditor(parent, option, index);
    auto *combo = new QComboBox(parent);
    for (const QString &c : choices) combo->addItem(c.isEmpty() ? tr("(none)") : c, c);
    combo->setMaxVisibleItems(20);
    combo->setEditable(index.data(ChannelModel::FreeTextRole).toBool());   // tones: any value may be typed
    return combo;
}

void ChannelDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const {
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        const QString current = index.data(Qt::EditRole).toString();
        const int i = combo->findData(current);
        if (i >= 0) combo->setCurrentIndex(i);
        else if (combo->isEditable()) combo->setEditText(current);
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void ChannelDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const {
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        QString value = combo->currentText();
        if (!combo->isEditable() || combo->findText(value) == combo->currentIndex()) value = combo->currentData().toString();
        if (value == tr("(none)")) value.clear();
        model->setData(index, value, Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

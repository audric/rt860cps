#include "zonepanel.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

using rt860::Zone;

ZonePanel::ZonePanel(QWidget *parent) : QWidget(parent) {
    m_zones = new QListWidget;
    m_zones->setMinimumWidth(230);
    m_zones->setMaximumWidth(300);
    m_name = new QLineEdit;
    m_name->setMaxLength(16);
    m_name->setPlaceholderText(tr("Zone name"));
    m_count = new QLabel;
    m_members = new QListWidget;
    m_members->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_available = new QListWidget;
    m_available->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter channels"));
    m_filter->setClearButtonEnabled(true);
    m_add = new QPushButton(tr("← Add"));
    m_remove = new QPushButton(tr("Remove →"));
    m_up = new QPushButton(tr("Move up"));
    m_down = new QPushButton(tr("Move down"));

    auto *buttons = new QVBoxLayout;
    buttons->addStretch(1);
    for (QPushButton *b : {m_add, m_remove, m_up, m_down}) buttons->addWidget(b);
    buttons->addStretch(2);

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Name")), 0, 0);
    grid->addWidget(m_name, 0, 1);
    grid->addWidget(m_count, 1, 0, 1, 2);
    grid->addWidget(m_members, 2, 0, 1, 2);
    grid->addLayout(buttons, 2, 2);
    grid->addWidget(new QLabel(tr("Channels not in this zone")), 0, 3);
    grid->addWidget(m_filter, 1, 3);
    grid->addWidget(m_available, 2, 3);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_zones);
    layout->addLayout(grid, 1);

    connect(m_zones, &QListWidget::currentRowChanged, this, [this] { showZone(); });
    connect(m_name, &QLineEdit::editingFinished, this, &ZonePanel::rename);
    connect(m_filter, &QLineEdit::textChanged, this, [this] { showZone(); });
    connect(m_add, &QPushButton::clicked, this, &ZonePanel::addSelected);
    connect(m_available, &QListWidget::itemDoubleClicked, this, [this] { addSelected(); });
    connect(m_remove, &QPushButton::clicked, this, &ZonePanel::removeSelected);
    connect(m_up, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(m_down, &QPushButton::clicked, this, [this] { moveSelected(1); });
    setEnabled(false);
}

void ZonePanel::setImages(QByteArray *work, const QByteArray *base) {
    m_work = work;
    m_base = base;
    reload();
}

int ZonePanel::currentZone() const { return m_zones->currentRow(); }

QString ZonePanel::channelLabel(int channel) const {
    if (channel >= rt860::ChannelCount) return tr("%1  (not a channel number)").arg(channel + 1);
    const rt860::Channel c = rt860::decodeChannel(*m_work, channel);
    if (c.empty) return tr("%1  (empty channel)").arg(channel + 1);
    return QStringLiteral("%1  %2  %3").arg(channel + 1).arg(c.name, rt860::freqText(c.rxFreq));
}

void ZonePanel::refreshZoneRow(int zone) {
    const Zone z = rt860::decodeZone(*m_work, zone);
    QListWidgetItem *item = m_zones->item(zone);
    QString label = QStringLiteral("%1  %2").arg(zone + 1, 3).arg(z.name.isEmpty() ? tr("(no name)") : z.name);
    if (!z.channels.isEmpty()) label += QStringLiteral("  (%1)").arg(z.channels.size());
    item->setText(label);
    QFont f = font();
    f.setBold(m_base && m_base->size() == rt860::ImageSize && rt860::decodeZone(*m_base, zone) != z);
    item->setFont(f);
    item->setForeground(z.channels.isEmpty() && z.name.isEmpty() ? palette().brush(QPalette::Disabled, QPalette::Text)
                                                                 : palette().brush(QPalette::Text));
}

void ZonePanel::reload() {
    setEnabled(ready());
    if (!ready()) return;
    const int keep = qMax(0, currentZone());
    {
        const QSignalBlocker block(m_zones);
        if (m_zones->count() != rt860::ZoneCount) {
            m_zones->clear();
            for (int i = 0; i < rt860::ZoneCount; ++i) m_zones->addItem(QString());
        }
        for (int i = 0; i < rt860::ZoneCount; ++i) refreshZoneRow(i);
        m_zones->setCurrentRow(keep);
    }
    showZone();
}

void ZonePanel::showZone() {
    if (!ready() || currentZone() < 0) return;
    const Zone z = rt860::decodeZone(*m_work, currentZone());
    {
        const QSignalBlocker block(m_name);
        m_name->setText(z.name);
    }
    m_count->setText(tr("%1 of %2 channels").arg(z.channels.size()).arg(rt860::ZoneSlots));
    const int memberRow = m_members->currentRow();
    m_members->clear();
    for (quint16 ch : z.channels) {
        auto *item = new QListWidgetItem(channelLabel(ch));
        item->setData(Qt::UserRole, int(ch));
        m_members->addItem(item);
    }
    if (memberRow >= 0 && memberRow < m_members->count()) m_members->setCurrentRow(memberRow);
    m_available->clear();
    const QString filter = m_filter->text().trimmed();
    for (int ch = 0; ch < rt860::ChannelCount; ++ch) {
        if (z.channels.contains(quint16(ch)) || rt860::decodeChannel(*m_work, ch).empty) continue;
        const QString label = channelLabel(ch);
        if (!filter.isEmpty() && !label.contains(filter, Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, ch);
        m_available->addItem(item);
    }
    m_add->setEnabled(z.channels.size() < rt860::ZoneSlots);
}

void ZonePanel::store(const Zone &zone) {
    const int z = currentZone();
    if (rt860::decodeZone(*m_work, z) == zone) return;
    rt860::encodeZone(*m_work, z, zone);
    refreshZoneRow(z);
    showZone();
    emit edited();
}

void ZonePanel::rename() {
    if (!ready() || currentZone() < 0) return;
    Zone z = rt860::decodeZone(*m_work, currentZone());
    const QString name = m_name->text().trimmed();
    if (rt860::decodeName(rt860::encodeName(name, 16)) != name) {
        emit rejected(tr("Zone names can have at most 16 characters (Chinese characters count as two) and must exist in GBK."));
        showZone();
        return;
    }
    z.name = name;
    store(z);
}

void ZonePanel::addSelected() {
    if (!ready() || currentZone() < 0) return;
    Zone z = rt860::decodeZone(*m_work, currentZone());
    int skipped = 0;
    for (const QListWidgetItem *item : m_available->selectedItems()) {
        if (z.channels.size() >= rt860::ZoneSlots) { ++skipped; continue; }
        z.channels.append(quint16(item->data(Qt::UserRole).toInt()));
    }
    if (skipped) emit rejected(tr("A zone holds at most %1 channels; %2 were not added.").arg(rt860::ZoneSlots).arg(skipped));
    store(z);
}

void ZonePanel::removeSelected() {
    if (!ready() || currentZone() < 0) return;
    Zone z = rt860::decodeZone(*m_work, currentZone());
    QList<int> rows;
    for (const QListWidgetItem *item : m_members->selectedItems()) rows << m_members->row(item);
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows) z.channels.removeAt(r);
    store(z);
}

void ZonePanel::moveSelected(int delta) {
    if (!ready() || currentZone() < 0) return;
    Zone z = rt860::decodeZone(*m_work, currentZone());
    const int row = m_members->currentRow(), to = row + delta;
    if (row < 0 || to < 0 || to >= z.channels.size()) return;
    z.channels.swapItemsAt(row, to);
    m_members->setCurrentRow(to);
    store(z);
    m_members->setCurrentRow(to);
}

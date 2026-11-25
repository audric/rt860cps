#include "mainwindow.h"
#include "channelmodel.h"
#include "presets.h"
#include "serialtransport.h"
#include "settingspanel.h"
#include "zonepanel.h"
#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

using rt860::Channel;

// "1 channel" / "3 channels"
static QString counted(int n, const char *one, const char *many) {
    return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
}

void RadioJob::run() {
    try {
        rt860::SerialTransport transport(port);
        if (kind == Read) {
            rt860::Radio radio(transport);
            radio.connect();
            image = radio.readImage([this](int d, int t) { emit progress(tr("Reading the radio"), d, t); });
            radio.end();
        } else {
            options.progress = [this](const QString &stage, int d, int t) { emit progress(stage, d, t); };
            result = rt860::writeToRadio(transport, base, work, options);
        }
    } catch (const std::exception &e) {
        error = QString::fromStdString(e.what());
    }
}

// What differs between two images, in words, for the confirmation before writing.
static QStringList describeChanges(const QByteArray &from, const QByteArray &to) {
    QStringList lines;
    for (int i = 0; i < rt860::ChannelCount; ++i) {
        const int at = rt860::ChannelsOffset + i * rt860::ChannelSize;
        if (from.mid(at, rt860::ChannelSize) == to.mid(at, rt860::ChannelSize)) continue;
        const Channel a = rt860::decodeChannel(from, i), b = rt860::decodeChannel(to, i);
        if (a.empty && !b.empty)
            lines << QObject::tr("Channel %1: new, %2 MHz \"%3\"").arg(i + 1).arg(rt860::freqText(b.rxFreq), b.name);
        else if (!a.empty && b.empty)
            lines << QObject::tr("Channel %1: deleted (was %2 MHz \"%3\")").arg(i + 1).arg(rt860::freqText(a.rxFreq), a.name);
        else if (!b.empty)
            lines << QObject::tr("Channel %1 \"%2\": changed").arg(i + 1).arg(b.name);
    }
    for (int i = 0; i < rt860::ZoneCount; ++i) {
        const int at = rt860::ZonesOffset + i * rt860::ZoneSize;
        if (from.mid(at, rt860::ZoneSize) == to.mid(at, rt860::ZoneSize)) continue;
        const rt860::Zone b = rt860::decodeZone(to, i);
        lines << QObject::tr("Zone %1 \"%2\": changed, now %3 channels").arg(i + 1).arg(b.name).arg(b.channels.size());
    }
    for (const rt860::Setting &s : rt860::settings()) {
        const QVariant a = rt860::settingValue(from, s.id), b = rt860::settingValue(to, s.id);
        if (a == b) continue;
        auto show = [&](const QVariant &v) {
            if (s.kind == rt860::Setting::List) return v.toInt() >= 0 ? s.options.at(v.toInt()) : QObject::tr("(unknown)");
            if (s.kind == rt860::Setting::Bool) return v.toBool() ? QObject::tr("on") : QObject::tr("off");
            if (s.kind == rt860::Setting::Mhz) return QString::number(v.toDouble(), 'f', 5);
            return v.toString();
        };
        lines << QStringLiteral("%1 / %2: %3 → %4").arg(s.group, s.label, show(a), show(b));
    }
    for (int i = 0; i < rt860::DtmfCount; ++i)
        if (rt860::dtmfCode(from, i) != rt860::dtmfCode(to, i))
            lines << QObject::tr("DTMF code %1: %2 → %3").arg(i + 1).arg(rt860::dtmfCode(from, i), rt860::dtmfCode(to, i));
    return lines;
}

MainWindow::MainWindow() {
    buildUi();
    refreshPorts();
    updateState();
}

void MainWindow::buildUi() {
    setWindowTitle(tr("RT-860 CPS"));
    resize(1360, 800);

    m_model = new ChannelModel(this);
    m_filter = new ChannelFilter(this);
    m_filter->setSourceModel(m_model);

    // ---- toolbar
    auto *bar = addToolBar(tr("Main"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addWidget(new QLabel(tr(" Port ")));
    m_ports = new QComboBox;
    m_ports->setMinimumWidth(150);
    bar->addWidget(m_ports);
    auto *rescan = bar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), tr("Rescan"));
    rescan->setToolTip(tr("Look for serial ports again"));
    connect(rescan, &QAction::triggered, this, &MainWindow::refreshPorts);
    bar->addSeparator();
    m_readAct = bar->addAction(style()->standardIcon(QStyle::SP_ArrowDown), tr("Read from radio"));
    m_writeAct = bar->addAction(style()->standardIcon(QStyle::SP_ArrowUp), tr("Write to radio"));
    bar->addSeparator();
    auto *newAct = bar->addAction(style()->standardIcon(QStyle::SP_FileIcon), tr("New"));
    auto *openAct = bar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open file…"));
    m_saveAct = bar->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save file as…"));
    bar->addSeparator();
    m_revertAct = bar->addAction(style()->standardIcon(QStyle::SP_DialogResetButton), tr("Undo all changes"));
    m_readAct->setShortcut(QKeySequence(tr("Ctrl+R")));
    m_writeAct->setShortcut(QKeySequence(tr("Ctrl+W")));
    openAct->setShortcut(QKeySequence::Open);
    m_saveAct->setShortcut(QKeySequence::Save);
    connect(m_readAct, &QAction::triggered, this, &MainWindow::readFromRadio);
    connect(m_writeAct, &QAction::triggered, this, [this] { writeToRadio(); });
    connect(newAct, &QAction::triggered, this, [this] {
        if (!confirmDiscard()) return;
        m_fromRadio = false;
        setImage(rt860::blankImage(), tr("New configuration"));
        notify(tr("New, empty configuration with the Radtel software's default settings."));
    });
    connect(openAct, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(m_saveAct, &QAction::triggered, this, &MainWindow::saveFileDialog);
    connect(m_revertAct, &QAction::triggered, this, &MainWindow::revert);

    // ---- menu
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addActions({m_readAct, m_writeAct});
    fileMenu->addSeparator();
    fileMenu->addActions({newAct, openAct, m_saveAct});
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Open backup folder"), this, [this] {
        QDir().mkpath(backupDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(backupDir()));
    });
    fileMenu->addAction(tr("Choose backup folder…"), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Backup folder"), backupDir());
        if (!dir.isEmpty()) QSettings().setValue("backupDir", dir);
        updateState();
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto *channelsMenu = menuBar()->addMenu(tr("&Channels"));
    m_importAct = channelsMenu->addAction(tr("Add ready-made channel lists…"), this, [this] { importPresets(); });
    m_importAct->setToolTip(tr("CB, marine VHF, PMR446, LPD433, Freenet, Italian airports and repeaters, and more"));

    // ---- channels tab
    auto *channelsPage = new QWidget;
    auto *filterEdit = new QLineEdit;
    filterEdit->setPlaceholderText(tr("Filter by name, frequency or number"));
    filterEdit->setClearButtonEnabled(true);
    auto *hideEmpty = new QCheckBox(tr("Hide empty slots"));
    m_table = new QTableView;
    m_table->setModel(m_filter);
    m_table->setItemDelegate(new ChannelDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 8);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_table->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(filterEdit, &QLineEdit::textChanged, m_filter, &ChannelFilter::setText);
    connect(hideEmpty, &QCheckBox::toggled, m_filter, &ChannelFilter::setHideEmpty);

    auto addTableAction = [this](const QString &text, const QKeySequence &key, void (MainWindow::*slot)()) {
        auto *a = new QAction(text, m_table);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(a, &QAction::triggered, this, slot);
        m_table->addAction(a);
        return a;
    };
    addTableAction(tr("Copy"), QKeySequence::Copy, &MainWindow::copyChannels);
    addTableAction(tr("Paste"), QKeySequence::Paste, &MainWindow::pasteChannels);
    addTableAction(tr("Delete channel"), QKeySequence::Delete, &MainWindow::deleteChannels);
    auto *up = new QAction(tr("Move up"), m_table);
    up->setShortcut(QKeySequence(tr("Alt+Up")));
    up->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(up, &QAction::triggered, this, [this] { moveChannels(-1); });
    auto *down = new QAction(tr("Move down"), m_table);
    down->setShortcut(QKeySequence(tr("Alt+Down")));
    down->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(down, &QAction::triggered, this, [this] { moveChannels(1); });
    m_table->addActions({up, down});
    auto *importButton = new QPushButton(tr("Add channel lists…"));
    importButton->setToolTip(m_importAct->toolTip());
    connect(importButton, &QPushButton::clicked, this, [this] { importPresets(); });
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(filterEdit, 1);
    filterRow->addWidget(hideEmpty);
    filterRow->addWidget(importButton);
    auto *channelsLayout = new QVBoxLayout(channelsPage);
    channelsLayout->addLayout(filterRow);
    channelsLayout->addWidget(m_table, 1);

    // ---- tabs
    auto *tabs = new QTabWidget;
    tabs->addTab(channelsPage, tr("Channels"));
    m_zones = new ZonePanel;
    tabs->addTab(m_zones, tr("Zones"));
    connect(m_zones, &ZonePanel::edited, this, &MainWindow::updateState);
    connect(m_zones, &ZonePanel::rejected, this, [this](const QString &why) { notify(why, true); });
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs] {
        if (tabs->currentWidget() == m_zones) m_zones->reload();      // channel names may have changed
    });
    for (const QString &g : rt860::settingGroups()) {
        auto *panel = new SettingsPanel(g);
        m_panels << panel;
        tabs->addTab(panel, g);
        connect(panel, &SettingsPanel::edited, this, &MainWindow::updateState);
        connect(panel, &SettingsPanel::rejected, this, [this](const QString &why) { notify(why, true); });
    }
    m_tabs = tabs;

    m_banner = new QLabel;
    m_banner->setWordWrap(true);
    m_banner->setMargin(8);
    m_banner->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_banner->hide();

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 0);
    layout->addWidget(m_banner);
    layout->addWidget(tabs, 1);
    setCentralWidget(central);

    m_status = new QLabel;
    statusBar()->addPermanentWidget(m_status, 1);

    connect(m_model, &ChannelModel::edited, this, &MainWindow::updateState);
    connect(m_model, &ChannelModel::rejected, this, [this](const QString &why) { notify(why, true); });
}

void MainWindow::showTab(int index) { static_cast<QTabWidget *>(m_tabs)->setCurrentIndex(index); }

QString MainWindow::backupDir() const {
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/backups");
    return QSettings().value("backupDir", fallback).toString();
}

void MainWindow::refreshPorts() {
    const QString current = m_ports->currentText();
    m_ports->clear();
    m_ports->addItems(rt860::availableSerialPorts());
    const int i = m_ports->findText(current);
    if (i >= 0) m_ports->setCurrentIndex(i);
    updateState();
}

void MainWindow::notify(const QString &text, bool warning) {
    m_banner->setText(text);
    const QPalette pal = palette();
    const QColor accent = warning ? QColor(0xb4, 0x53, 0x09) : pal.color(QPalette::Highlight);
    m_banner->setStyleSheet(QStringLiteral("QLabel { border: 1px solid %1; border-left: 5px solid %1; border-radius: 3px; }").arg(accent.name()));
    m_banner->setVisible(!text.isEmpty());
}

void MainWindow::updateState() {
    const bool have = m_work.size() == rt860::ImageSize;
    const bool port = m_ports->count() > 0;
    m_readAct->setEnabled(port);
    m_writeAct->setEnabled(have && port && m_radioSeen && (isDirty() || !m_fromRadio));
    m_writeAct->setToolTip(m_radioSeen ? tr("Write the changes to the radio")
                                       : tr("Read from the radio once first; writing is switched on after a successful read."));
    m_saveAct->setEnabled(have);
    m_revertAct->setEnabled(have && isDirty());
    m_tabs->setEnabled(have);
    m_importAct->setEnabled(have);

    QStringList parts;
    if (!port) parts << tr("No serial port found. Plug in the programming cable and press Rescan.");
    if (have) {
        parts << m_source;
        const int changes = int(describeChanges(m_base, m_work).size());
        parts << (isDirty() ? counted(qMax(1, changes), "unsaved change", "unsaved changes") : tr("No changes"));
        const int problems = m_model->problemCount();
        if (problems) parts << tr("%1 flagged").arg(counted(problems, "channel", "channels"));
    } else if (port) {
        parts << tr("Read from the radio or open a file to begin.");
    }
    m_status->setText(parts.join(QStringLiteral("   •   ")));
    setWindowTitle(have ? tr("%1%2 — RT-860 CPS").arg(m_source, isDirty() ? QStringLiteral(" *") : QString()) : tr("RT-860 CPS"));
}

void MainWindow::setImage(const QByteArray &image, const QString &source) {
    m_base = image;
    m_work = image;
    m_source = source;
    m_model->setImages(&m_work, &m_base);
    m_zones->setImages(&m_work, &m_base);
    for (SettingsPanel *p : m_panels) p->setImages(&m_work, &m_base);
    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(ChannelModel::Name, 150);
    m_table->setColumnWidth(ChannelModel::RxFreq, 110);
    m_table->setColumnWidth(ChannelModel::TxFreq, 110);
    updateState();
}

void MainWindow::reloadViews() {
    m_model->reload();
    m_zones->reload();
    for (SettingsPanel *p : m_panels) p->reload();
    updateState();
}

bool MainWindow::confirmDiscard() {
    if (m_work.size() != rt860::ImageSize || !isDirty()) return true;
    return QMessageBox::question(this, tr("Discard changes?"),
                                 tr("You have changes that were not written to the radio or saved to a file. Discard them?"),
                                 QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (confirmDiscard()) event->accept();
    else event->ignore();
}

void MainWindow::runJob(RadioJob *job, const QString &title, std::function<void(RadioJob *)> done) {
    job->setParent(this);
    job->port = m_ports->currentText();
    auto *dialog = new QProgressDialog(title, QString(), 0, 100, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->setValue(0);
    connect(job, &RadioJob::progress, dialog, [dialog](const QString &stage, int d, int t) {
        dialog->setLabelText(stage + QStringLiteral("…"));
        dialog->setMaximum(t);
        dialog->setValue(d);
    });
    connect(job, &QThread::finished, this, [this, job, dialog, done] {
        dialog->close();
        dialog->deleteLater();
        if (!job->error.isEmpty())
            QMessageBox::critical(this, tr("Radio"), job->error);
        else
            done(job);
        job->deleteLater();
    });
    job->start();
}

void MainWindow::readFromRadio() {
    if (!confirmDiscard()) return;
    auto *job = new RadioJob;
    job->kind = RadioJob::Read;
    runJob(job, tr("Reading the radio…"), [this](RadioJob *j) {
        m_fromRadio = true;
        m_radioSeen = true;
        setImage(j->image, tr("Radio on %1").arg(j->port));
        const int problems = m_model->problemCount();
        notify(problems ? tr("Read the radio. %1 hold values this program does not expect (marked with a warning sign); "
                             "if many are marked, the radio may store its data differently from what this program knows. "
                             "Save a copy to a file before changing anything.").arg(counted(problems, "channel", "channels"))
                        : tr("Read the radio. Save a copy to a file before changing anything."),
               problems > 0);
    });
}

void MainWindow::writeToRadio(bool overwriteConflicts) {
    if (m_work.size() != rt860::ImageSize) return;
    if (m_fromRadio) {
        startWrite(m_base, overwriteConflicts);
        return;
    }
    // The image came from a file: compare it with what the radio holds now.
    auto *job = new RadioJob;
    job->kind = RadioJob::Read;
    runJob(job, tr("Reading the radio to compare…"), [this](RadioJob *j) { startWrite(j->image, false); });
}

void MainWindow::startWrite(const QByteArray &base, bool overwriteConflicts) {
    const auto plan = rt860::planWrites(base, m_work);
    if (plan.isEmpty()) {
        QMessageBox::information(this, tr("Write to radio"), tr("The radio already holds these channels, zones and settings. Nothing to write."));
        return;
    }
    if (!overwriteConflicts) {
        QStringList lines = describeChanges(base, m_work);
        const int total = int(lines.size());
        if (total > 25) {
            lines = lines.mid(0, 25);
            lines << tr("… and %1 more").arg(total - 25);
        }
        if (lines.isEmpty()) lines << tr("Some bytes outside the listed channels, zones and settings differ.");
        QMessageBox box(QMessageBox::Question, tr("Write to radio"),
                        tr("Write %1 to the radio?").arg(counted(qMax(1, total), "change", "changes")), QMessageBox::Cancel, this);
        box.setInformativeText(tr("%1 of the radio's 184 memory blocks will be rewritten.\n\n"
                                  "A backup of the radio's current contents is saved first in:\n%2\n\n"
                                  "The radio's read/write password is left as it is. After writing, the program reads "
                                  "the radio again and compares.")
                                   .arg(plan.size() + (plan.contains(rt860::BlockRef{rt860::Cfg, 0}) ? 0 : 1))
                                   .arg(backupDir()));
        box.setDetailedText(lines.join('\n'));
        auto *go = box.addButton(tr("Write"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() != go) return;
    }

    auto *job = new RadioJob;
    job->kind = RadioJob::Write;
    job->base = base;
    job->work = m_work;
    job->options.backupDir = backupDir();
    job->options.overwriteConflicts = overwriteConflicts;
    runJob(job, tr("Writing to the radio…"), [this, base](RadioJob *j) {
        const rt860::WriteResult &r = j->result;
        // The Time area never travels to the radio; keep the one being edited.
        auto withTime = [this](QByteArray image) {
            image.replace(rt860::TimeOffset, rt860::TimeSize, m_work.mid(rt860::TimeOffset, rt860::TimeSize));
            return image;
        };
        switch (r.status) {
        case rt860::WriteResult::Ok:
            m_fromRadio = true;
            setImage(withTime(r.radioImage), tr("Radio on %1").arg(j->port));
            notify(tr("Written and verified. Backup of the previous contents: %1").arg(r.backupPath));
            break;
        case rt860::WriteResult::NothingToWrite:
            QMessageBox::information(this, tr("Write to radio"), tr("Nothing to write."));
            break;
        case rt860::WriteResult::Conflict: {
            QStringList where;
            for (const rt860::BlockRef &b : r.conflicts)
                where << QStringLiteral("%1 block %2").arg(QLatin1String(rt860::areaInfo(b.area).name)).arg(b.index);
            QMessageBox box(QMessageBox::Warning, tr("The radio changed"),
                            tr("The radio changed some of the same memory since it was read (for example through its own menu). Nothing was written."),
                            QMessageBox::Cancel, this);
            box.setInformativeText(tr("Read the radio again to see its current values, or overwrite them with yours."));
            box.setDetailedText(tr("Changed: %1").arg(where.join(QStringLiteral(", "))));
            auto *over = box.addButton(tr("Overwrite"), QMessageBox::DestructiveRole);
            box.exec();
            if (box.clickedButton() == over) startWrite(base, true);
            break;
        }
        case rt860::WriteResult::VerifyFailed: {
            QStringList where;
            for (const rt860::BlockRef &b : r.failed)
                where << QStringLiteral("%1 block %2").arg(QLatin1String(rt860::areaInfo(b.area).name)).arg(b.index);
            // Show what the radio really holds now, with the intended values as unsaved changes.
            const QByteArray wanted = m_work;
            m_fromRadio = true;
            setImage(withTime(r.radioImage), tr("Radio on %1").arg(j->port));
            m_work = wanted;
            reloadViews();
            QMessageBox box(QMessageBox::Critical, tr("Write to radio"),
                            tr("The radio did not keep everything that was written."), QMessageBox::Ok, this);
            box.setInformativeText(tr("The window now shows your values as unsaved changes against what the radio really holds. "
                                      "Try writing again. The radio as it was before is saved in:\n%1").arg(r.backupPath));
            box.setDetailedText(tr("Different after writing: %1").arg(where.join(QStringLiteral(", "))));
            box.exec();
            break;
        }
        }
    });
}

bool MainWindow::openFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open file"), tr("Cannot open %1: %2").arg(path, f.errorString()));
        return false;
    }
    const QByteArray data = f.readAll();
    if (data.size() != rt860::ImageSize) {
        QMessageBox::warning(this, tr("Open file"), tr("%1 is %2 bytes; an RT-860 file (.88dat) is exactly %3 bytes.").arg(path).arg(data.size()).arg(rt860::ImageSize));
        return false;
    }
    m_fromRadio = false;
    setImage(data, QFileInfo(path).fileName());
    notify(tr("Opened %1.").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::openFileDialog() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open file"), QSettings().value("lastDir").toString(),
                                                      tr("RT-860 files (*.88dat);;All files (*)"));
    if (path.isEmpty()) return;
    QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
    openFile(path);
}

void MainWindow::saveFileDialog() {
    QString path = QFileDialog::getSaveFileName(this, tr("Save file"), QSettings().value("lastDir").toString(),
                                                tr("RT-860 files (*.88dat)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".88dat");
    QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(m_work) != m_work.size()) {
        QMessageBox::warning(this, tr("Save file"), tr("Cannot save %1: %2").arg(path, f.errorString()));
        return;
    }
    notify(tr("Saved %1. Changes are still not on the radio until you write them.").arg(path));
}

void MainWindow::revert() {
    if (QMessageBox::question(this, tr("Undo all changes"), tr("Go back to the image as it was read or opened?")) != QMessageBox::Yes) return;
    m_work = m_base;
    reloadViews();
}

QList<int> MainWindow::selectedRows() const {
    QList<int> rows;
    for (const QModelIndex &i : m_table->selectionModel()->selectedRows()) rows << m_filter->mapToSource(i).row();
    if (rows.isEmpty() && m_table->currentIndex().isValid()) rows << m_filter->mapToSource(m_table->currentIndex()).row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

void MainWindow::copyChannels() {
    m_clipboard.clear();
    for (int r : selectedRows()) m_clipboard << m_model->channel(r);
    statusBar()->showMessage(tr("%1 copied").arg(counted(int(m_clipboard.size()), "channel", "channels")), 3000);
}

void MainWindow::pasteChannels() {
    const QList<int> rows = selectedRows();
    if (m_clipboard.isEmpty() || rows.isEmpty()) return;
    const int first = rows.first();
    const int count = qMin(int(m_clipboard.size()), rt860::ChannelCount - first);
    int overwritten = 0;
    for (int i = 0; i < count; ++i) overwritten += !m_model->channel(first + i).empty;
    if (overwritten && QMessageBox::question(this, tr("Paste"), tr("This replaces %1 starting at slot %2. Continue?").arg(counted(overwritten, "existing channel", "existing channels")).arg(first + 1)) != QMessageBox::Yes)
        return;
    for (int i = 0; i < count; ++i) m_model->setChannel(first + i, m_clipboard.at(i));
}

void MainWindow::deleteChannels() {
    for (int r : selectedRows())
        if (!m_model->channel(r).empty) m_model->setChannel(r, Channel{});
}

void MainWindow::moveChannels(int delta) {
    QList<int> rows = selectedRows();
    if (rows.isEmpty()) return;
    if (rows.first() + delta < 0 || rows.last() + delta >= rt860::ChannelCount) return;
    if (delta > 0) std::reverse(rows.begin(), rows.end());
    for (int r : rows) m_model->swapChannels(r, r + delta);
    m_table->clearSelection();
    QItemSelection selection;
    for (int r : rows) {
        const QModelIndex i = m_filter->mapFromSource(m_model->index(r + delta, 0));
        if (i.isValid()) selection.select(i, i);
    }
    m_table->selectionModel()->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    const QModelIndex cur = m_filter->mapFromSource(m_model->index(rows.last() + delta, qMax(0, m_table->currentIndex().column())));
    if (cur.isValid()) m_table->selectionModel()->setCurrentIndex(cur, QItemSelectionModel::NoUpdate);
}

QString MainWindow::userListsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/rt860cps/lists");
}

void MainWindow::importPresets(const QStringList &preselect) {
    if (m_work.size() != rt860::ImageSize) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add ready-made channel lists"));
    dialog.resize(680, 640);

    auto *list = new QListWidget;
    auto addItem = [list](const rt860::PresetGroup &g, bool checked) {
        auto *item = new QListWidgetItem(tr("%1  —  %2").arg(g.name, counted(int(g.channels.size()), "channel", "channels")));
        item->setToolTip(g.note);
        item->setData(Qt::UserRole, g.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        list->addItem(item);
    };
    for (const rt860::PresetGroup &g : rt860::presetGroups()) addItem(g, preselect.contains(g.id) || preselect.contains(QStringLiteral("all")));
    auto *openFolder = new QPushButton(tr("Open my lists folder"));
    openFolder->setToolTip(tr("Your own lists live in %1 as CSV files. A file named like a shipped list replaces it.").arg(userListsDir()));
    auto *reload = new QPushButton(tr("Reload"));
    reload->setToolTip(tr("Read the list files again after you change them"));
    auto *fileProblems = new QPushButton;
    fileProblems->setFlat(true);
    auto showFileProblems = [fileProblems] {
        const int n = int(rt860::presetProblems().size());
        fileProblems->setVisible(n > 0);
        fileProblems->setText(tr("%1 in the list files…").arg(counted(n, "problem", "problems")));
    };
    showFileProblems();
    connect(fileProblems, &QPushButton::clicked, &dialog, [&dialog] {
        QMessageBox box(QMessageBox::Information, tr("List files"), tr("Some rows of the list files were left out or changed."), QMessageBox::Ok, &dialog);
        box.setDetailedText(rt860::presetProblems().join(QLatin1Char('\n')));
        box.exec();
    });
    connect(openFolder, &QPushButton::clicked, &dialog, [this] {
        const QDir dir(userListsDir());
        dir.mkpath(QStringLiteral("."));
        const QString shipped = rt860::presetDirs().value(0);
        if (!QFile::exists(dir.filePath("README.md")) && QFile::exists(shipped + "/README.md")) QFile::copy(shipped + "/README.md", dir.filePath("README.md"));
        QFile where(dir.filePath("SHIPPED-LISTS.txt"));
        if (where.open(QIODevice::WriteOnly)) where.write(tr("The lists that come with the program are in:\n%1\nCopy one here to change it. See README.md for the format.\n").arg(shipped).toUtf8());
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()))) notify(tr("Your lists folder is %1").arg(dir.absolutePath()));
    });
    auto *note = new QLabel;
    note->setWordWrap(true);
    note->setMinimumHeight(note->fontMetrics().lineSpacing() * 2);

    int firstFree = 0;
    while (firstFree < rt860::ChannelCount - 1 && !rt860::decodeChannel(m_work, firstFree).empty) ++firstFree;
    auto *start = new QSpinBox;
    start->setRange(1, rt860::ChannelCount);
    start->setValue(firstFree + 1);
    auto *zones = new QCheckBox(tr("Also make a zone for each list"));
    zones->setChecked(true);
    auto *airLock = new QCheckBox(tr("Make 118–137 MHz receive-only (sets a lock range in the Lock ranges tab)"));
    airLock->setChecked(true);
    auto *summary = new QLabel;
    auto *room = new QProgressBar;
    room->setTextVisible(true);
    auto *warning = new QLabel;
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color: #c0392b; font-weight: bold;"));
    auto *legal = new QLabel(tr("Channels are added with transmit switched on at the listed frequency. Transmitting on many of "
                                "these bands needs a licence or an approved radio; check what applies where you are."));
    legal->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *addButton = buttons->addButton(tr("Add"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto chosen = [&] {
        QList<const rt860::PresetGroup *> groups;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked)
                if (const rt860::PresetGroup *g = rt860::findPresetGroup(list->item(i)->data(Qt::UserRole).toString())) groups << g;
        return groups;
    };
    auto refresh = [&] {
        const QList<const rt860::PresetGroup *> picked = chosen();
        int wanted = 0, tooBigForZone = 0;
        for (const rt860::PresetGroup *g : picked) {
            wanted += int(g->channels.size());
            tooBigForZone += g->channels.size() > rt860::ZoneSlots;
        }
        const int freeAll = rt860::freeSlotsFrom(m_work, 0);
        const int free = rt860::freeSlotsFrom(m_work, start->value() - 1);
        const int zonesFree = rt860::freeZones(m_work);
        const int zonesWanted = zones->isChecked() ? int(picked.size()) : 0;
        const bool channelsFit = wanted <= free, zonesFit = zonesWanted <= zonesFree;

        // the bar shows the radio's channel memory: what is used now plus what is ticked
        room->setRange(0, rt860::ChannelCount);
        room->setValue(qMin(rt860::ChannelCount, rt860::ChannelCount - freeAll + wanted));
        room->setFormat(tr("%1 in use + %2 selected of %3").arg(rt860::ChannelCount - freeAll).arg(wanted).arg(rt860::ChannelCount));
        room->setStyleSheet(channelsFit ? QString() : QStringLiteral("QProgressBar { text-align: center; } QProgressBar::chunk { background: #c0392b; }"));

        QStringList lines, warnings;
        QString channelLine = tr("Channels: %1 selected, %2 free").arg(wanted).arg(free);
        if (start->value() > 1 && free != freeAll) channelLine += tr(" from slot %1 on (%2 free in all)").arg(start->value()).arg(freeAll);
        if (channelsFit && wanted) channelLine += tr(", %1 left afterwards").arg(free - wanted);
        lines << channelLine;
        if (zones->isChecked()) lines << tr("Zones: %1 needed, %2 free of %3").arg(zonesWanted).arg(zonesFree).arg(rt860::ZoneCount);
        if (!channelsFit) warnings << tr("Not enough room: %1 too many. Untick some lists%2.").arg(counted(wanted - free, "channel", "channels"),
                                                                                              free != freeAll ? tr(" or start at a lower slot") : QString());
        if (!zonesFit) warnings << tr("Not enough free zones: %1 too many. Untick some lists or the zone option.").arg(zonesWanted - zonesFree);
        if (tooBigForZone && zones->isChecked()) warnings << tr("A zone holds %1 channels: longer lists get only their first %1 in the zone.").arg(rt860::ZoneSlots);
        if (wanted == 0) lines << tr("Tick the lists to add. Channels already in the radio are not touched.");
        summary->setText(lines.join(QLatin1Char('\n')));
        warning->setText(warnings.join(QLatin1Char('\n')));
        warning->setVisible(!warnings.isEmpty());
        addButton->setEnabled(wanted > 0 && channelsFit && zonesFit);
        bool air = false;
        for (const rt860::PresetGroup *g : chosen()) air = air || rt860::presetUsesAirband(*g);
        airLock->setVisible(air);
    };
    connect(list, &QListWidget::itemChanged, &dialog, refresh);
    connect(start, &QSpinBox::valueChanged, &dialog, refresh);
    connect(zones, &QCheckBox::toggled, &dialog, refresh);
    connect(list, &QListWidget::currentItemChanged, &dialog, [note](QListWidgetItem *item) { note->setText(item ? item->toolTip() : QString()); });
    list->setCurrentRow(0);
    refresh();
    connect(reload, &QPushButton::clicked, &dialog, [&] {
        rt860::setPresetDirs(rt860::presetDirs());
        list->clear();
        for (const rt860::PresetGroup &g : rt860::presetGroups()) addItem(g, false);
        list->setCurrentRow(0);
        showFileProblems();
        refresh();
    });

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Start at slot")));
    row->addWidget(start);
    row->addSpacing(16);
    row->addWidget(zones);
    row->addStretch(1);
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(openFolder);
    folderRow->addWidget(reload);
    folderRow->addWidget(fileProblems);
    folderRow->addStretch(1);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(list, 1);
    layout->addWidget(note);
    layout->addLayout(folderRow);
    layout->addLayout(row);
    layout->addWidget(airLock);
    layout->addWidget(room);
    layout->addWidget(summary);
    layout->addWidget(warning);
    layout->addWidget(legal);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    QByteArray image = m_work;      // all lists or none
    int added = 0, first = -1;
    bool air = false;
    QStringList names;
    for (const rt860::PresetGroup *g : chosen()) {
        const rt860::ImportResult r = rt860::importPreset(image, *g, start->value() - 1, zones->isChecked());
        if (!r.ok) {
            notify(tr("Nothing was added. %1").arg(r.error), true);
            return;
        }
        added += int(r.placed.size());
        if (first < 0) first = r.placed.first();
        names << g->name;
        air = air || rt860::presetUsesAirband(*g);
    }
    QString lockNote;
    if (air && airLock->isChecked()) {
        const int range = rt860::makeAirbandReceiveOnly(image);
        if (range > 0) lockNote = tr(" Lock range %1 now makes 118–137 MHz receive-only.").arg(range);
        else if (range < 0) lockNote = tr(" All four lock ranges are in use, so 118–137 MHz was NOT made receive-only: do not transmit on the air band channels.");
    } else if (air) {
        lockNote = tr(" Do not transmit on the air band channels.");
    }
    m_work = image;
    reloadViews();
    if (first >= 0) {
        const QModelIndex at = m_filter->mapFromSource(m_model->index(first, ChannelModel::Name));
        if (at.isValid()) m_table->scrollTo(at, QAbstractItemView::PositionAtTop);
    }
    notify(tr("Added %1 (%2), starting at slot %3. They are on the radio only after you write.")
               .arg(counted(added, "channel", "channels"), names.join(QStringLiteral(", "))).arg(first + 1) + lockNote,
           lockNote.contains(QLatin1String("NOT")));
}

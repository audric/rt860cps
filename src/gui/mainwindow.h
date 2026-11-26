#pragma once
#include "sync.h"
#include <QMainWindow>
#include <QThread>

class ChannelModel;
class ChannelFilter;
class SettingsPanel;
class ZonePanel;
class QComboBox;
class QLabel;
class QTableView;
class QProgressDialog;

// One conversation with the radio, run off the GUI thread.
class RadioJob : public QThread {
    Q_OBJECT
public:
    enum Kind { Read, Write };
    Kind kind = Read;
    QString port;
    QByteArray base, work;           // Write only
    rt860::WriteOptions options;        // Write only
    // results
    QString error;
    QByteArray image;
    rt860::WriteResult result;

signals:
    void progress(const QString &stage, int done, int total);

protected:
    void run() override;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool openFile(const QString &path);
    void showTab(int index);   // used by the screenshot option
    void importPresets(const QStringList &preselect = {});
    static QString userListsDir();   // where the user keeps their own channel lists

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void refreshPorts();
    void readFromRadio();
    void writeToRadio(bool overwriteConflicts = false);
    void startWrite(const QByteArray &base, bool overwriteConflicts);
    void openFileDialog();
    void saveFileDialog();
    void revert();
    void setImage(const QByteArray &image, const QString &source);
    void reloadViews();
    void updateState();
    void notify(const QString &text, bool warning = false);
    bool confirmDiscard();
    bool isDirty() const { return m_work != m_base; }
    QString backupDir() const;
    void runJob(RadioJob *job, const QString &title, std::function<void(RadioJob *)> done);
    // channel actions
    QList<int> selectedRows() const;
    void copyChannels();
    void pasteChannels();
    void deleteChannels();
    void moveChannels(int delta);

    QByteArray m_base, m_work;
    QByteArray m_saved;             // the image last saved to a file, if any
    QString m_source;
    bool m_fromRadio = false;
    QList<rt860::Channel> m_clipboard;

    ChannelModel *m_model = nullptr;
    ChannelFilter *m_filter = nullptr;
    QTableView *m_table = nullptr;
    QList<SettingsPanel *> m_panels;
    ZonePanel *m_zones = nullptr;
    QComboBox *m_ports = nullptr;
    QLabel *m_banner = nullptr;
    QLabel *m_status = nullptr;
    QAction *m_readAct = nullptr, *m_writeAct = nullptr, *m_saveAct = nullptr, *m_revertAct = nullptr, *m_importAct = nullptr;
    QWidget *m_tabs = nullptr;
};

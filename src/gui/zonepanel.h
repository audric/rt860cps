#pragma once
#include "layout.h"
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// The 256 zones: pick a zone on the left, edit its name and member channels.
class ZonePanel : public QWidget {
    Q_OBJECT
public:
    explicit ZonePanel(QWidget *parent = nullptr);
    void setImages(QByteArray *work, const QByteArray *base);
    void reload();

signals:
    void edited();
    void rejected(const QString &reason);

private:
    bool ready() const { return m_work && m_work->size() == rt860::ImageSize; }
    int currentZone() const;
    void refreshZoneRow(int zone);
    void showZone();
    void store(const rt860::Zone &zone);
    void rename();
    void addSelected();
    void removeSelected();
    void moveSelected(int delta);
    QString channelLabel(int channel) const;

    QByteArray *m_work = nullptr;
    const QByteArray *m_base = nullptr;
    QListWidget *m_zones = nullptr;
    QLineEdit *m_name = nullptr;
    QLabel *m_count = nullptr;
    QListWidget *m_members = nullptr;
    QListWidget *m_available = nullptr;
    QLineEdit *m_filter = nullptr;
    QPushButton *m_add = nullptr, *m_remove = nullptr, *m_up = nullptr, *m_down = nullptr;
};

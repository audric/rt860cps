#include "settingspanel.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

using rt860::Setting;

SettingsPanel::SettingsPanel(const QString &group, QWidget *parent) : QWidget(parent), m_group(group) {
    auto *inner = new QWidget;
    auto *form = new QFormLayout(inner);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(16);

    for (const Setting &s : rt860::settings()) {
        if (s.group != group) continue;
        const QString id = s.id;
        QWidget *editor = nullptr;
        switch (s.kind) {
        case Setting::List: {
            auto *combo = new QComboBox;
            for (int i = 0; i < s.options.size(); ++i)
                combo->addItem(s.options.at(i), i);
            combo->setMaxVisibleItems(20);
            combo->setPlaceholderText(tr("(not set)"));
            combo->setFocusPolicy(Qt::StrongFocus);
            connect(combo, &QComboBox::activated, this, [this, id, combo](int) { apply(id, combo->currentData()); });
            editor = combo;
            break;
        }
        case Setting::Bool: {
            auto *box = new QCheckBox;
            connect(box, &QCheckBox::clicked, this, [this, id](bool on) { apply(id, on); });
            editor = box;
            break;
        }
        case Setting::Int: {
            auto *spin = new QSpinBox;
            spin->setRange(s.min, s.max);
            spin->setKeyboardTracking(false);
            connect(spin, &QSpinBox::valueChanged, this, [this, id](int v) { apply(id, v); });
            editor = spin;
            break;
        }
        case Setting::Text: {
            auto *edit = new QLineEdit;
            edit->setMaxLength(s.maxLength);
            edit->setMinimumWidth(qBound(120, 12 * s.maxLength, 320));
            connect(edit, &QLineEdit::editingFinished, this, [this, id, edit] { apply(id, edit->text()); });
            editor = edit;
            break;
        }
        case Setting::Mhz: {
            auto *spin = new QDoubleSpinBox;
            spin->setDecimals(5);
            spin->setRange(18.0, 999.99999);
            spin->setKeyboardTracking(false);
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, id](double v) { apply(id, v); });
            editor = spin;
            break;
        }
        }
        auto *label = new QLabel(s.label);
        label->setBuddy(editor);
        form->addRow(label, editor);
        m_editors.insert(id, editor);
        m_labels.insert(id, label);
    }

    if (group == QLatin1String("DTMF")) {
        for (int i = 0; i < rt860::DtmfCount; ++i) {
            auto *edit = new QLineEdit;
            edit->setMaxLength(rt860::DtmfMaxLength);
            edit->setMinimumWidth(200);
            edit->setPlaceholderText(tr("0-9 A-D * #"));
            connect(edit, &QLineEdit::editingFinished, this, [this, i, edit] { applyDtmf(i, edit->text()); });
            auto *label = new QLabel(tr("Code %1").arg(i + 1));
            form->addRow(label, edit);
            m_dtmfEdits << edit;
            m_dtmfLabels << label;
        }
    }

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
    setEnabled(false);
}

void SettingsPanel::setImages(QByteArray *work, const QByteArray *base) {
    m_work = work;
    m_base = base;
    reload();
}

void SettingsPanel::reload() {
    const bool ready = m_work && m_work->size() == rt860::ImageSize;
    setEnabled(ready);
    if (!ready) return;
    for (const Setting &s : rt860::settings())
        if (s.group == m_group) refreshOne(s);
    for (int i = 0; i < m_dtmfEdits.size(); ++i) {
        const QString code = rt860::dtmfCode(*m_work, i);
        const QSignalBlocker block(m_dtmfEdits.at(i));
        m_dtmfEdits.at(i)->setText(code);
        QFont f = font();
        f.setBold(m_base && m_base->size() == rt860::ImageSize && rt860::dtmfCode(*m_base, i) != code);
        m_dtmfLabels.at(i)->setFont(f);
    }
}

void SettingsPanel::applyDtmf(int index, const QString &code) {
    if (m_loading || !m_work) return;
    if (rt860::dtmfCode(*m_work, index) == code.trimmed().toUpper()) return;
    if (!rt860::setDtmfCode(*m_work, index, code)) emit rejected(tr("DTMF codes can only use 0-9, A-D, * and #."));
    else emit edited();
    reload();
}

void SettingsPanel::refreshOne(const Setting &s) {
    QWidget *editor = m_editors.value(s.id);
    const QVariant v = rt860::settingValue(*m_work, s.id);
    m_loading = true;
    {
        const QSignalBlocker block(editor);
        QString note;
        switch (s.kind) {
        case Setting::List: {
            auto *combo = static_cast<QComboBox *>(editor);
            combo->setCurrentIndex(combo->findData(v.toInt()));
            if (combo->currentIndex() < 0) note = tr("The radio holds a value this program does not know.");
            break;
        }
        case Setting::Bool:
            static_cast<QCheckBox *>(editor)->setChecked(v.toBool());
            break;
        case Setting::Int:
            static_cast<QSpinBox *>(editor)->setValue(v.toInt());
            if (v.toInt() < s.min || v.toInt() > s.max)
                note = tr("The radio holds a value outside the range the Radtel software allows.");
            break;
        case Setting::Text:
            static_cast<QLineEdit *>(editor)->setText(v.toString());
            break;
        case Setting::Mhz:
            static_cast<QDoubleSpinBox *>(editor)->setValue(v.toDouble());
            break;
        }
        editor->setToolTip(note);
    }
    m_loading = false;
    QFont f = font();
    f.setBold(m_base && m_base->size() == rt860::ImageSize && rt860::settingValue(*m_base, s.id) != v);
    m_labels.value(s.id)->setFont(f);
}

void SettingsPanel::apply(const QString &id, const QVariant &value) {
    if (m_loading || !m_work) return;
    const Setting *s = rt860::findSetting(id);
    if (rt860::settingValue(*m_work, id) == value) return;
    if (!rt860::setSettingValue(*m_work, id, value)) {
        emit rejected(tr("That value is not accepted for \"%1\".").arg(s->label));
    } else {
        emit edited();
    }
    reload();    // some settings share bytes, so refresh the whole group
}

// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 FreeCAD project contributors

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>

#include <App/Application.h>
#include <Base/Parameter.h>

#include "DlgSettingsRadialMenu.h"
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/RadialMenu.h>
#include <Gui/RadialMenuConfig.h>

using namespace Gui::Dialog;

/* TRANSLATOR Gui::Dialog::DlgSettingsRadialMenu */

namespace
{

constexpr int maxSlots = Gui::RadialMenuConfig::MaxSlots;

const char* const slotNames[8] = {
    QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "North"),
    QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "North-east"),
    QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "East"),
    QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "South-east"),
    QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "South"),
    QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "South-west"),
    QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "West"),
    QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "North-west"),
};

struct ContextEntry
{
    const char* key;
    const char* label;
};

const ContextEntry contexts[] = {
    {"Any", QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "Any")},
    {"Edit", QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "Edit mode")},
    {"Empty", QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "Empty space")},
    {"Object", QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "Object")},
    {"Face", QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "Face")},
    {"Edge", QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "Edge")},
    {"Vertex", QT_TRANSLATE_NOOP("Gui::Dialog::DlgSettingsRadialMenu", "Vertex")},
};

QIcon commandIcon(Gui::Command* cmd)
{
    if (Gui::Action* action = cmd->getAction()) {
        QIcon icon = action->action()->icon();
        if (!icon.isNull()) {
            return icon;
        }
    }
    if (const char* pixmap = cmd->getPixmap()) {
        return Gui::BitmapFactory().iconFromTheme(pixmap);
    }
    return QIcon();
}

QString commandText(Gui::Command* cmd)
{
    QString text;
    if (Gui::Action* action = cmd->getAction()) {
        text = action->action()->text();
    }
    if (text.isEmpty() && cmd->getMenuText()) {
        text = QCoreApplication::translate(cmd->className(), cmd->getMenuText());
    }
    if (text.isEmpty()) {
        text = QString::fromUtf8(cmd->getName());
    }
    text.remove(QLatin1Char('&'));
    return text;
}

/// Searchable list of all registered commands grouped by category.
class CommandPicker: public QDialog
{
public:
    explicit CommandPicker(QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(QCoreApplication::translate("Gui::Dialog::DlgSettingsRadialMenu",
                                                   "Select command"));
        resize(460, 520);
        auto layout = new QVBoxLayout(this);
        filter = new QLineEdit(this);
        filter->setClearButtonEnabled(true);
        filter->setPlaceholderText(
            QCoreApplication::translate("Gui::Dialog::DlgSettingsRadialMenu", "Search commands")
        );
        layout->addWidget(filter);
        tree = new QTreeWidget(this);
        tree->setHeaderHidden(true);
        layout->addWidget(tree);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);

        std::map<QString, QTreeWidgetItem*> groups;
        auto cmds = Gui::Application::Instance->commandManager().getAllCommands();
        for (Gui::Command* cmd : cmds) {
            const QString group = QString::fromUtf8(cmd->getGroupName());
            auto& parentItem = groups[group];
            if (!parentItem) {
                parentItem = new QTreeWidgetItem(tree, QStringList(group));
            }
            auto item = new QTreeWidgetItem(parentItem);
            item->setText(0, commandText(cmd));
            item->setIcon(0, commandIcon(cmd));
            item->setToolTip(0, QString::fromUtf8(cmd->getName()));
            item->setData(0, Qt::UserRole, QString::fromUtf8(cmd->getName()));
        }
        tree->sortItems(0, Qt::AscendingOrder);

        connect(filter, &QLineEdit::textChanged, this, [this](const QString& text) {
            applyFilter(text);
        });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
            if (item->parent()) {
                accept();
            }
        });
        filter->setFocus();
    }

    std::string selected() const
    {
        auto item = tree->currentItem();
        if (!item || !item->parent()) {
            return {};
        }
        return item->data(0, Qt::UserRole).toString().toStdString();
    }

private:
    void applyFilter(const QString& text)
    {
        for (int g = 0; g < tree->topLevelItemCount(); ++g) {
            auto group = tree->topLevelItem(g);
            int visible = 0;
            for (int i = 0; i < group->childCount(); ++i) {
                auto child = group->child(i);
                const bool match = text.isEmpty()
                    || child->text(0).contains(text, Qt::CaseInsensitive)
                    || child->data(0, Qt::UserRole).toString().contains(text, Qt::CaseInsensitive);
                child->setHidden(!match);
                visible += match ? 1 : 0;
            }
            group->setHidden(visible == 0);
            group->setExpanded(!text.isEmpty() && visible > 0);
        }
    }

    QLineEdit* filter;
    QTreeWidget* tree;
};

}  // namespace

DlgSettingsRadialMenu::DlgSettingsRadialMenu(QWidget* parent)
    : PreferencePage(parent)
{
    auto main = new QVBoxLayout(this);

    generalBox = new QGroupBox(this);
    auto generalForm = new QFormLayout(generalBox);
    enabledBox = new QCheckBox(generalBox);
    thresholdBox = new QSpinBox(generalBox);
    thresholdBox->setRange(2, 200);
    thresholdBox->setSuffix(QStringLiteral(" px"));
    thresholdBox->setValue(10);
    thresholdLabel = new QLabel(generalBox);
    generalForm->addRow(enabledBox);
    generalForm->addRow(thresholdLabel, thresholdBox);
    main->addWidget(generalBox);

    slotsBox = new QGroupBox(this);
    auto slotsLayout = new QVBoxLayout(slotsBox);
    auto combos = new QFormLayout();
    workbenchCombo = new QComboBox(slotsBox);
    contextCombo = new QComboBox(slotsBox);
    workbenchLabel = new QLabel(slotsBox);
    contextLabel = new QLabel(slotsBox);
    combos->addRow(workbenchLabel, workbenchCombo);
    combos->addRow(contextLabel, contextCombo);
    countLabel = new QLabel(slotsBox);
    countBox = new QSpinBox(slotsBox);
    countBox->setRange(Gui::RadialMenuConfig::MinSlots, Gui::RadialMenuConfig::MaxSlots);
    countBox->setValue(Gui::RadialMenuConfig::DefaultSlots);
    combos->addRow(countLabel, countBox);
    slotsLayout->addLayout(combos);

    statusLabel = new QLabel(slotsBox);
    statusLabel->setWordWrap(true);
    slotsLayout->addWidget(statusLabel);

    auto row = new QHBoxLayout();
    preview = new Gui::RadialMenuView(slotsBox);
    row->addWidget(preview, 0, Qt::AlignTop);
    slotList = new QListWidget(slotsBox);
    slotList->setIconSize(QSize(24, 24));
    row->addWidget(slotList, 1);
    auto buttonsLayout = new QVBoxLayout();
    assignButton = new QPushButton(slotsBox);
    clearButton = new QPushButton(slotsBox);
    upButton = new QPushButton(slotsBox);
    downButton = new QPushButton(slotsBox);
    resetButton = new QPushButton(slotsBox);
    buttonsLayout->addWidget(assignButton);
    buttonsLayout->addWidget(clearButton);
    buttonsLayout->addWidget(upButton);
    buttonsLayout->addWidget(downButton);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(resetButton);
    row->addLayout(buttonsLayout);
    slotsLayout->addLayout(row);
    main->addWidget(slotsBox, 1);

    fillCombos();
    retranslate();

    connect(workbenchCombo, &QComboBox::currentIndexChanged, this, [this]() {
        onCombinationChanged();
    });
    connect(contextCombo, &QComboBox::currentIndexChanged, this, [this]() {
        onCombinationChanged();
    });
    connect(assignButton, &QPushButton::clicked, this, [this]() {
        onAssign();
    });
    connect(slotList, &QListWidget::itemDoubleClicked, this, [this]() {
        onAssign();
    });
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        onClear();
    });
    connect(upButton, &QPushButton::clicked, this, [this]() {
        onMove(-1);
    });
    connect(downButton, &QPushButton::clicked, this, [this]() {
        onMove(1);
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        onReset();
    });
    connect(slotList, &QListWidget::currentRowChanged, this, [this](int row) {
        preview->setSelectedSlot(row);
        updateButtons();
    });
    connect(preview, &Gui::RadialMenuView::slotClicked, this, [this](int slot) {
        slotList->setCurrentRow(slot);
    });
    connect(preview, &Gui::RadialMenuView::slotActivated, this, [this](int slot) {
        slotList->setCurrentRow(slot);
        onAssign();
    });
    connect(countBox, &QSpinBox::valueChanged, this, [this](int value) {
        onCountChanged(value);
    });
}

DlgSettingsRadialMenu::~DlgSettingsRadialMenu() = default;

void DlgSettingsRadialMenu::fillCombos()
{
    const QSignalBlocker block1(workbenchCombo);
    const QSignalBlocker block2(contextCombo);
    workbenchCombo->clear();
    workbenchCombo->addItem(tr("Global (all workbenches)"), QStringLiteral("Global"));
    std::vector<std::pair<QString, QString>> entries;
    for (const QString& name : Gui::Application::Instance->workbenches()) {
        entries.emplace_back(Gui::Application::Instance->workbenchMenuText(name), name);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.first.localeAwareCompare(b.first) < 0;
    });
    for (const auto& entry : entries) {
        workbenchCombo->addItem(entry.first, entry.second);
    }
    contextCombo->clear();
    for (const auto& ctx : contexts) {
        contextCombo->addItem(tr(ctx.label), QString::fromLatin1(ctx.key));
    }
}

void DlgSettingsRadialMenu::retranslate()
{
    setWindowTitle(tr("Radial Menu"));
    generalBox->setTitle(tr("General"));
    slotsBox->setTitle(tr("Commands"));
    enabledBox->setText(tr("Enable the radial menu (right mouse button drag in the 3D view)"));
    thresholdLabel->setText(tr("Drag distance before the menu appears:"));
    workbenchLabel->setText(tr("Workbench:"));
    contextLabel->setText(tr("Context:"));
    countLabel->setText(tr("Number of commands:"));
    assignButton->setText(tr("Assign command..."));
    clearButton->setText(tr("Clear slot"));
    upButton->setText(tr("Move up"));
    downButton->setText(tr("Move down"));
    resetButton->setText(tr("Reset to default"));
    for (int i = 0; i < contextCombo->count() && i < int(std::size(contexts)); ++i) {
        contextCombo->setItemText(i, tr(contexts[i].label));
    }
    workbenchCombo->setItemText(0, tr("Global (all workbenches)"));
    if (!shownKey.empty()) {
        showCurrent();
    }
}

std::string DlgSettingsRadialMenu::currentKey() const
{
    return workbenchCombo->currentData().toString().toStdString() + "/"
        + contextCombo->currentData().toString().toStdString();
}

bool DlgSettingsRadialMenu::customCommands(
    const std::string& key,
    std::vector<std::string>& out,
    int& count
) const
{
    auto it = pending.find(key);
    if (it != pending.end()) {
        if (it->second.reset) {
            return false;
        }
        out = it->second.commands;
        count = it->second.count;
        return true;
    }
    if (Gui::RadialMenuConfig::hasCommandsFor(key)) {
        out = Gui::RadialMenuConfig::commandsFor(key);
        count = Gui::RadialMenuConfig::slotCountFor(key);
        return true;
    }
    return false;
}

std::vector<std::string> DlgSettingsRadialMenu::resolve(
    const std::string& key,
    QString* source,
    int* countOut
) const
{
    const auto pos = key.rfind('/');
    const std::string workbench = key.substr(0, pos);
    const std::string context = key.substr(pos + 1);
    std::vector<std::string> cmds;
    int count = Gui::RadialMenuConfig::DefaultSlots;
    auto setSource = [source](const QString& text) {
        if (source) {
            *source = text;
        }
    };

    if (customCommands(key, cmds, count)) {
        setSource(tr("Customized for this workbench and context."));
    }
    else if (customCommands(workbench + "/Any", cmds, count)) {
        setSource(tr("Not customized: using the settings of this workbench for any context."));
    }
    else if (customCommands("Global/" + context, cmds, count)) {
        setSource(tr("Not customized: using the global settings for this context."));
    }
    else {
        cmds = Gui::RadialMenuConfig::defaultCommandsFor(key);
        count = Gui::RadialMenuConfig::DefaultSlots;
        setSource(tr("Not customized: using the entries of the context menu of the 3D view."));
    }
    cmds.resize(maxSlots);
    if (countOut) {
        *countOut = count;
    }
    return cmds;
}

void DlgSettingsRadialMenu::showCurrent()
{
    shownKey = currentKey();
    shown = resolve(shownKey, &sourceText, &shownCount);
    {
        const QSignalBlocker blocker(countBox);
        countBox->setValue(shownCount);
    }
    refreshList();
}

void DlgSettingsRadialMenu::refreshList()
{
    const int row = std::max(0, slotList->currentRow());
    const QSignalBlocker blocker(slotList);
    slotList->clear();
    auto& manager = Gui::Application::Instance->commandManager();
    for (int i = 0; i < shownCount; ++i) {
        QString text;
        QIcon icon;
        if (shown[i].empty()) {
            text = tr("(empty)");
        }
        else if (Gui::Command* cmd = manager.getCommandByName(shown[i].c_str())) {
            text = commandText(cmd);
            icon = commandIcon(cmd);
        }
        else {
            text = tr("%1 (unavailable)").arg(QString::fromStdString(shown[i]));
        }
        auto item = new QListWidgetItem(icon, QStringLiteral("%1: %2").arg(slotLabel(i, shownCount), text));
        if (!shown[i].empty()) {
            item->setToolTip(QString::fromStdString(shown[i]));
        }
        slotList->addItem(item);
    }
    slotList->setCurrentRow(std::min(row, shownCount - 1));
    preview->setSelectedSlot(slotList->currentRow());
    updatePreview();
    updateStatus();
    updateButtons();
}

QString DlgSettingsRadialMenu::slotLabel(int slot, int count) const
{
    if (count == 8) {
        return tr(slotNames[slot]);
    }
    if (count == 4) {
        return tr(slotNames[slot * 2]);
    }
    if (slot == 0) {
        return tr("Slot %1 (north)").arg(slot + 1);
    }
    return tr("Slot %1").arg(slot + 1);
}

void DlgSettingsRadialMenu::updatePreview()
{
    std::vector<std::string> cmds(shown.begin(), shown.begin() + shownCount);
    auto items = Gui::RadialMenu::buildItems(cmds);
    for (auto& item : items) {
        item.enabled = !item.command.empty();  // commands may be inactive here, show them anyway
    }
    preview->setItems(items);
}

void DlgSettingsRadialMenu::updateStatus()
{
    QString text = sourceText;
    const bool hidden = std::any_of(shown.begin() + shownCount, shown.end(), [](const std::string& s) {
        return !s.empty();
    });
    if (hidden) {
        text += QLatin1Char(' ')
            + tr("Commands of the removed slots are kept until you apply the settings.");
    }
    statusLabel->setText(text);
}

void DlgSettingsRadialMenu::updateButtons()
{
    const int slot = currentSlot();
    const bool hasSlot = slot >= 0 && slot < shownCount;
    assignButton->setEnabled(hasSlot);
    clearButton->setEnabled(hasSlot && !shown[slot].empty());
    upButton->setEnabled(hasSlot && slot > 0);
    downButton->setEnabled(hasSlot && slot < shownCount - 1);

    auto it = pending.find(shownKey);
    const bool customized = it != pending.end() ? !it->second.reset
                                                : Gui::RadialMenuConfig::hasCommandsFor(shownKey);
    resetButton->setEnabled(customized);
}

int DlgSettingsRadialMenu::currentSlot() const
{
    return slotList->currentRow();
}

void DlgSettingsRadialMenu::setCurrentCommands(const std::vector<std::string>& cmds, int count)
{
    shown = cmds;
    shown.resize(maxSlots);
    shownCount = std::clamp(count, Gui::RadialMenuConfig::MinSlots, maxSlots);
    Pending& entry = pending[shownKey];
    entry.reset = false;
    entry.commands = shown;
    entry.count = shownCount;
    sourceText = tr("Customized for this workbench and context.");
    refreshList();
}

void DlgSettingsRadialMenu::onCombinationChanged()
{
    if (!loading) {
        showCurrent();
    }
}

void DlgSettingsRadialMenu::onAssign()
{
    const int slot = currentSlot();
    if (slot < 0) {
        return;
    }
    CommandPicker picker(this);
    if (picker.exec() != QDialog::Accepted) {
        return;
    }
    const std::string name = picker.selected();
    if (name.empty()) {
        return;
    }
    auto cmds = shown;
    cmds[slot] = name;
    setCurrentCommands(cmds, shownCount);
}

void DlgSettingsRadialMenu::onClear()
{
    const int slot = currentSlot();
    if (slot < 0) {
        return;
    }
    auto cmds = shown;
    cmds[slot].clear();
    setCurrentCommands(cmds, shownCount);
}

void DlgSettingsRadialMenu::onMove(int delta)
{
    const int slot = currentSlot();
    const int target = slot + delta;
    if (slot < 0 || target < 0 || target >= shownCount) {
        return;
    }
    auto cmds = shown;
    std::swap(cmds[slot], cmds[target]);
    setCurrentCommands(cmds, shownCount);
    slotList->setCurrentRow(target);
}

void DlgSettingsRadialMenu::onCountChanged(int count)
{
    if (loading || count == shownCount) {
        return;
    }
    // commands beyond the new count stay in memory until the settings are applied
    setCurrentCommands(shown, count);
}

void DlgSettingsRadialMenu::onReset()
{
    Pending& entry = pending[shownKey];
    entry.reset = true;
    entry.commands.clear();
    showCurrent();
}

void DlgSettingsRadialMenu::saveSettings()
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(Gui::RadialMenuConfig::groupPath());
    hGrp->SetBool("Enabled", enabledBox->isChecked());
    hGrp->SetInt("DragThreshold", thresholdBox->value());
    for (const auto& [key, entry] : pending) {
        if (entry.reset) {
            Gui::RadialMenuConfig::resetCommandsFor(key);
        }
        else {
            Gui::RadialMenuConfig::setCommandsFor(key, entry.commands, entry.count);
        }
    }
    pending.clear();
    showCurrent();
}

void DlgSettingsRadialMenu::loadSettings()
{
    loading = true;
    enabledBox->setChecked(Gui::RadialMenuConfig::isEnabled());
    thresholdBox->setValue(Gui::RadialMenuConfig::dragThreshold());
    pending.clear();

    // Preselect the workbench that is currently active
    const std::string key = Gui::RadialMenuConfig::contextKey();
    const int wb = workbenchCombo->findData(QString::fromStdString(key.substr(0, key.rfind('/'))));
    workbenchCombo->setCurrentIndex(wb >= 0 ? wb : 0);
    contextCombo->setCurrentIndex(0);
    loading = false;
    showCurrent();
}

void DlgSettingsRadialMenu::resetSettingsToDefaults()
{
    enabledBox->setChecked(true);
    thresholdBox->setValue(10);
}

void DlgSettingsRadialMenu::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslate();
    }
    else {
        QWidget::changeEvent(e);
    }
}

#include "moc_DlgSettingsRadialMenu.cpp"

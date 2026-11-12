// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 FreeCAD project contributors

#pragma once

#include <map>
#include <string>
#include <vector>

#include "PropertyPage.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace Gui
{
class RadialMenuView;

namespace Dialog
{

/**
 * Preference page to configure the radial menu: enable it, set the drag threshold and assign
 * commands to the slots (4 to 12) for every combination of workbench and context.
 *
 * Edits are collected in memory and written to the parameters in saveSettings().
 */
class DlgSettingsRadialMenu: public PreferencePage
{
    Q_OBJECT

public:
    explicit DlgSettingsRadialMenu(QWidget* parent = nullptr);
    ~DlgSettingsRadialMenu() override;

    void saveSettings() override;
    void loadSettings() override;
    void resetSettingsToDefaults() override;

protected:
    void changeEvent(QEvent* e) override;

private:
    struct Pending
    {
        bool reset = false;
        std::vector<std::string> commands;  // all RadialMenu::MaxSlots entries
        int count = 8;
    };

    void retranslate();
    void fillCombos();
    std::string currentKey() const;
    bool customCommands(const std::string& key, std::vector<std::string>& out, int& count) const;
    std::vector<std::string> resolve(const std::string& key, QString* source, int* count) const;
    void showCurrent();
    void setCurrentCommands(const std::vector<std::string>& cmds, int count);
    void updatePreview();
    void updateStatus();
    QString slotLabel(int slot, int count) const;
    void refreshList();
    void updateButtons();
    int currentSlot() const;

    void onCombinationChanged();
    void onAssign();
    void onClear();
    void onMove(int delta);
    void onReset();
    void onCountChanged(int count);

private:
    QGroupBox* generalBox;
    QGroupBox* slotsBox;
    QCheckBox* enabledBox;
    QLabel* thresholdLabel;
    QSpinBox* thresholdBox;
    QLabel* workbenchLabel;
    QComboBox* workbenchCombo;
    QLabel* contextLabel;
    QComboBox* contextCombo;
    QLabel* statusLabel;
    QLabel* countLabel;
    QSpinBox* countBox;
    Gui::RadialMenuView* preview;
    QListWidget* slotList;
    QPushButton* assignButton;
    QPushButton* clearButton;
    QPushButton* upButton;
    QPushButton* downButton;
    QPushButton* resetButton;

    std::map<std::string, Pending> pending;
    std::string shownKey;
    std::vector<std::string> shown;  // all MaxSlots entries, only the first shownCount are used
    int shownCount = 8;
    QString sourceText;
    bool loading = false;
};

}  // namespace Dialog
}  // namespace Gui

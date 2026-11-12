// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 FreeCAD project contributors

/***************************************************************************
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#pragma once

#include <string>
#include <vector>

#include <QIcon>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QWidget>
#include <QFont>
#include <QPalette>

#include <FCGlobal.h>

class QPainter;

namespace Gui
{

/**
 * A circular menu with a configurable number of command slots (MinSlots to MaxSlots), laid out
 * clockwise starting at north.
 *
 * The widget is purely visual: it does not take mouse input, the caller feeds it the cursor
 * position while the mouse button is held. The selected slot is a function of the cursor
 * direction relative to the centre, see slotAt().
 */
class GuiExport RadialMenu: public QWidget
{
    Q_OBJECT

public:
    struct Item
    {
        std::string command;  // empty: unused slot
        QString text;
        QIcon icon;
        bool enabled = false;
    };

    static constexpr int MinSlots = 4;
    static constexpr int MaxSlots = 12;
    static constexpr int DefaultSlots = 8;
    static constexpr int InnerRadius = 34;  // dead zone, logical pixels
    static constexpr int OuterRadius = 112;

    explicit RadialMenu(const std::vector<Item>& items, const QPoint& globalCenter);
    ~RadialMenu() override;

    /// Items for the given command names, one per entry (unknown commands give empty slots).
    static std::vector<Item> buildItems(const std::vector<std::string>& commands);

    /// Slot index (0 = north, clockwise) for an offset from the centre, or -1 in the dead zone.
    static int slotAt(const QPoint& offset, int slotCount);

    /// Size of the widget that holds the ring (also used by RadialMenuView).
    static QSize ringWidgetSize();
    /// Centre of the ring inside a widget of the given width.
    static QPointF ringCenter(int widgetWidth);
    /**
     * Paint the ring with the given items. `hovered` is highlighted like in the popup, `selected`
     * (or -1) is drawn with an additional accent outline.
     */
    static void paintRing(
        QPainter& painter,
        const QPalette& palette,
        const QFont& font,
        const QPointF& center,
        const std::vector<Item>& items,
        int hovered,
        int selected = -1
    );

    /// Update the highlighted slot from the global cursor position.
    void setCursorPos(const QPoint& globalPos);
    int hoveredSlot() const
    {
        return hovered;
    }

Q_SIGNALS:
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<Item> items;
    QPoint center;  // global
    int hovered = -1;
};

/**
 * Non-popup widget that draws a radial menu with the same look as RadialMenu and reports which
 * wedge is clicked. It is used to edit the menu in the preferences.
 */
class GuiExport RadialMenuView: public QWidget
{
    Q_OBJECT

public:
    explicit RadialMenuView(QWidget* parent = nullptr);

    void setItems(const std::vector<RadialMenu::Item>& items);
    void setSelectedSlot(int slot);
    int selectedSlot() const
    {
        return selected;
    }

Q_SIGNALS:
    /// A wedge was clicked.
    void slotClicked(int slot);
    /// A wedge was double clicked.
    void slotActivated(int slot);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int slotAtPos(const QPoint& pos) const;

    std::vector<RadialMenu::Item> items;
    int hovered = -1;
    int selected = -1;
};

}  // namespace Gui

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
#include <QString>
#include <QWidget>

#include <FCGlobal.h>

namespace Gui
{

/**
 * A circular menu with up to eight command slots (N, NE, E, SE, S, SW, W, NW).
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

    static constexpr int SlotCount = 8;
    static constexpr int InnerRadius = 34;  // dead zone, logical pixels
    static constexpr int OuterRadius = 112;

    explicit RadialMenu(const std::vector<Item>& items, const QPoint& globalCenter);
    ~RadialMenu() override;

    /// Items for the given command names (missing commands give empty slots).
    static std::vector<Item> buildItems(const std::vector<std::string>& commands);

    /// Slot index (0 = north, clockwise) for an offset from the centre, or -1 in the dead zone.
    static int slotAt(const QPoint& offset);

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

}  // namespace Gui

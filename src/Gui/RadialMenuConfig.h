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

#include <FCGlobal.h>

namespace Gui
{

/**
 * Configuration of the radial command menu.
 *
 * Settings live in the parameter group
 * "User parameter:BaseApp/Preferences/View/RadialMenu":
 *  - bool "Enabled" (default true)
 *  - int "DragThreshold" in pixels (default 10)
 *  - subgroups "<Workbench>/<Context>" with a string "Commands": semicolon separated
 *    command names, an empty name is an empty slot (at most MaxSlots entries).
 *
 * A context key has the form "<Workbench>/<Context>" where Context is one of
 * Edit, Empty, Face, Edge, Vertex, Object. The groups "<Workbench>/Any" and
 * "Global/<Context>" are used as fallbacks.
 */
class GuiExport RadialMenuConfig
{
public:
    static constexpr int MaxSlots = 8;

    /// Parameter group path of the radial menu settings.
    static const char* groupPath();

    /// Whether the radial menu is enabled at all.
    static bool isEnabled();
    /// Distance in pixels the cursor must be dragged before the menu appears.
    static int dragThreshold();

    /// Context key for the active workbench, edit mode and selection.
    static std::string contextKey();

    /// Names of the commands to show for the given context key (always MaxSlots entries,
    /// empty strings are empty slots).
    static std::vector<std::string> commandsFor(const std::string& contextKey);

    /// Built-in default derived from the classic context menu of the 3D view.
    static std::vector<std::string> defaultCommandsFor(const std::string& contextKey);

    /// Store a user configuration for the given context key.
    static void setCommandsFor(const std::string& contextKey, const std::vector<std::string>& cmds);

    /// Remove the user configuration of the given context key.
    static void resetCommandsFor(const std::string& contextKey);

    /// Whether a user configuration exists for exactly this context key.
    static bool hasCommandsFor(const std::string& contextKey);
};

}  // namespace Gui

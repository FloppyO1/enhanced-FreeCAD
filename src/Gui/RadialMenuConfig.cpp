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

#include <algorithm>
#include <cctype>
#include <sstream>

#include <App/Application.h>
#include <Base/Parameter.h>

#include "RadialMenuConfig.h"
#include "Application.h"
#include "Command.h"
#include "Document.h"
#include "MenuManager.h"
#include "Selection.h"
#include "WorkbenchManager.h"

using namespace Gui;

namespace
{

constexpr const char* commandsKey = "Commands";

ParameterGrp::handle rootGroup()
{
    return App::GetApplication().GetParameterGroupByPath(RadialMenuConfig::groupPath());
}

std::vector<std::string> splitCommands(const std::string& text)
{
    std::vector<std::string> result;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ';')) {
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        result.push_back(item);
    }
    result.resize(RadialMenuConfig::MaxSlots);
    return result;
}

std::string joinCommands(const std::vector<std::string>& cmds)
{
    std::string text;
    for (std::size_t i = 0; i < cmds.size() && i < std::size_t(RadialMenuConfig::MaxSlots); ++i) {
        if (i > 0) {
            text += ';';
        }
        text += cmds[i];
    }
    return text;
}

bool splitKey(const std::string& key, std::string& workbench, std::string& context)
{
    auto pos = key.rfind('/');
    if (pos == std::string::npos) {
        return false;
    }
    workbench = key.substr(0, pos);
    context = key.substr(pos + 1);
    return !workbench.empty() && !context.empty();
}

// Reads the commands without creating parameter groups; returns false if not configured.
bool readCommands(const std::string& workbench, const std::string& context, std::string& text)
{
    auto root = rootGroup();
    if (!root->HasGroup(workbench.c_str())) {
        return false;
    }
    auto wbGroup = root->GetGroup(workbench.c_str());
    if (!wbGroup->HasGroup(context.c_str())) {
        return false;
    }
    auto group = wbGroup->GetGroup(context.c_str());
    const char* missing = "\x01";
    text = group->GetASCII(commandsKey, missing);
    return text != missing;
}

enum class ElementKind
{
    Face,
    Edge,
    Vertex,
    Object
};

ElementKind elementKind(const char* subName)
{
    if (!subName || !*subName) {
        return ElementKind::Object;
    }
    std::string sub(subName);
    auto pos = sub.rfind('.');
    std::string last = (pos == std::string::npos) ? sub : sub.substr(pos + 1);
    if (last.empty()) {
        return ElementKind::Object;
    }
    auto matches = [&last](const std::string& prefix) {
        return last.size() > prefix.size() && last.compare(0, prefix.size(), prefix) == 0
            && std::all_of(last.begin() + prefix.size(), last.end(), [](unsigned char c) {
                   return std::isdigit(c) != 0;
               });
    };
    if (matches("Face")) {
        return ElementKind::Face;
    }
    if (matches("Edge")) {
        return ElementKind::Edge;
    }
    if (matches("Vertex")) {
        return ElementKind::Vertex;
    }
    return ElementKind::Object;
}

void collectCommands(MenuItem* item, std::vector<std::string>& out)
{
    const auto& commandManager = Application::Instance->commandManager();
    for (MenuItem* child : item->getItems()) {
        if (out.size() >= std::size_t(RadialMenuConfig::MaxSlots)) {
            return;
        }
        if (child->hasItems()) {
            continue;  // submenus are skipped
        }
        const std::string name = child->command();
        if (name.empty() || name == "Separator" || !commandManager.getCommandByName(name.c_str())) {
            continue;
        }
        if (std::find(out.begin(), out.end(), name) == out.end()) {
            out.push_back(name);
        }
    }
}

}  // namespace

const char* RadialMenuConfig::groupPath()
{
    return "User parameter:BaseApp/Preferences/View/RadialMenu";
}

bool RadialMenuConfig::isEnabled()
{
    return rootGroup()->GetBool("Enabled", true);
}

int RadialMenuConfig::dragThreshold()
{
    return std::max(2, int(rootGroup()->GetInt("DragThreshold", 10)));
}

std::string RadialMenuConfig::contextKey()
{
    std::string workbench = WorkbenchManager::instance()->activeName();
    if (workbench.empty()) {
        workbench = "Global";
    }

    std::string context = "Empty";
    const Gui::Document* doc = Application::Instance->activeDocument();
    if (doc && doc->getInEdit()) {
        context = "Edit";
    }
    else {
        const auto selection = Selection().getSelection(nullptr, ResolveMode::NoResolve);
        if (!selection.empty()) {
            ElementKind kind = elementKind(selection.front().SubName);
            for (const auto& sel : selection) {
                if (elementKind(sel.SubName) != kind) {
                    kind = ElementKind::Object;
                    break;
                }
            }
            switch (kind) {
                case ElementKind::Face:
                    context = "Face";
                    break;
                case ElementKind::Edge:
                    context = "Edge";
                    break;
                case ElementKind::Vertex:
                    context = "Vertex";
                    break;
                default:
                    context = "Object";
                    break;
            }
        }
    }
    return workbench + "/" + context;
}

std::vector<std::string> RadialMenuConfig::commandsFor(const std::string& contextKey)
{
    std::string workbench;
    std::string context;
    if (splitKey(contextKey, workbench, context)) {
        std::string text;
        if (readCommands(workbench, context, text) || readCommands(workbench, "Any", text)
            || readCommands("Global", context, text)) {
            return splitCommands(text);
        }
    }
    return defaultCommandsFor(contextKey);
}

std::vector<std::string> RadialMenuConfig::defaultCommandsFor(const std::string& contextKey)
{
    (void)contextKey;
    // The default mirrors the classic context menu of the 3D view for the current state.
    std::vector<std::string> result;
    MenuItem view;
    Application::Instance->setupContextMenu("View", &view);
    collectCommands(&view, result);
    result.resize(MaxSlots);
    return result;
}

void RadialMenuConfig::setCommandsFor(const std::string& contextKey, const std::vector<std::string>& cmds)
{
    std::string workbench;
    std::string context;
    if (!splitKey(contextKey, workbench, context)) {
        return;
    }
    auto group = rootGroup()->GetGroup(workbench.c_str())->GetGroup(context.c_str());
    group->SetASCII(commandsKey, joinCommands(cmds));
}

void RadialMenuConfig::resetCommandsFor(const std::string& contextKey)
{
    std::string workbench;
    std::string context;
    if (!splitKey(contextKey, workbench, context)) {
        return;
    }
    auto root = rootGroup();
    if (root->HasGroup(workbench.c_str())) {
        root->GetGroup(workbench.c_str())->RemoveGrp(context.c_str());
    }
}

bool RadialMenuConfig::hasCommandsFor(const std::string& contextKey)
{
    std::string workbench;
    std::string context;
    std::string text;
    return splitKey(contextKey, workbench, context) && readCommands(workbench, context, text);
}

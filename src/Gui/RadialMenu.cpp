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

#include <cmath>

#include <QApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include "RadialMenu.h"
#include "Action.h"
#include "Application.h"
#include "Command.h"

using namespace Gui;

namespace
{
constexpr int margin = 8;
constexpr double gapDegrees = 3.0;
constexpr int iconSize = 32;
constexpr int labelHeight = 30;
constexpr double pi = 3.14159265358979323846;
}  // namespace

RadialMenu::RadialMenu(const std::vector<Item>& itemList, const QPoint& globalCenter)
    : QWidget(nullptr)
    , items(itemList)
    , center(globalCenter)
{
    items.resize(SlotCount);
    setWindowFlags(
        Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
        | Qt::WindowTransparentForInput
    );
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_DeleteOnClose);

    const int side = 2 * (OuterRadius + margin);
    setFixedSize(side, side + labelHeight);
    move(center - QPoint(side / 2, side / 2));

    qApp->installEventFilter(this);
}

RadialMenu::~RadialMenu() = default;

std::vector<RadialMenu::Item> RadialMenu::buildItems(const std::vector<std::string>& commands)
{
    std::vector<Item> result(SlotCount);
    const auto& manager = Application::Instance->commandManager();
    for (int i = 0; i < SlotCount && i < int(commands.size()); ++i) {
        if (commands[i].empty()) {
            continue;
        }
        Command* cmd = manager.getCommandByName(commands[i].c_str());
        if (!cmd) {
            continue;
        }
        Item& item = result[i];
        item.command = commands[i];
        item.enabled = cmd->isActive();
        if (Action* action = cmd->getAction()) {
            QAction* qaction = action->action();
            item.text = qaction->text();
            item.text.remove(QLatin1Char('&'));
            item.icon = qaction->icon();
        }
        else {
            item.text = QString::fromUtf8(commands[i].c_str());
        }
    }
    return result;
}

int RadialMenu::slotAt(const QPoint& offset)
{
    const double dist = std::hypot(double(offset.x()), double(offset.y()));
    if (dist < InnerRadius) {
        return -1;
    }
    // angle clockwise from north
    double angle = std::atan2(double(offset.x()), double(-offset.y())) * 180.0 / pi;
    if (angle < 0) {
        angle += 360.0;
    }
    return int(std::floor((angle + 22.5) / 45.0)) % SlotCount;
}

void RadialMenu::setCursorPos(const QPoint& globalPos)
{
    int slot = slotAt(globalPos - center);
    if (slot >= 0 && items[slot].command.empty()) {
        slot = -1;
    }
    if (slot != hovered) {
        hovered = slot;
        update();
    }
}

bool RadialMenu::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        Q_EMIT cancelled();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void RadialMenu::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPalette pal = palette();
    const QPointF c(width() / 2.0, margin + OuterRadius);
    const QRectF outer(c.x() - OuterRadius, c.y() - OuterRadius, 2.0 * OuterRadius, 2.0 * OuterRadius);
    const QRectF inner(c.x() - InnerRadius, c.y() - InnerRadius, 2.0 * InnerRadius, 2.0 * InnerRadius);

    QColor base = pal.color(QPalette::Window);
    base.setAlpha(225);
    QColor edge = pal.color(QPalette::Mid);
    QColor accent = pal.color(QPalette::Highlight);
    QColor text = pal.color(QPalette::WindowText);

    // wedges
    for (int i = 0; i < SlotCount; ++i) {
        const double mid = 90.0 - i * 45.0;  // Qt angles: counter-clockwise from east
        const double start = mid - 22.5 + gapDegrees / 2.0;
        const double span = 45.0 - gapDegrees;

        QPainterPath path;
        path.arcMoveTo(outer, start);
        path.arcTo(outer, start, span);
        path.arcTo(inner, start + span, -span);
        path.closeSubpath();

        const Item& item = items[i];
        const bool isHovered = (i == hovered);
        QColor fill = base;
        if (item.command.empty()) {
            fill.setAlpha(110);
        }
        else if (isHovered && item.enabled) {
            fill = accent;
            fill.setAlpha(235);
        }
        else if (isHovered) {
            fill = edge;
            fill.setAlpha(200);
        }
        p.setPen(QPen(edge, 1.0));
        p.setBrush(fill);
        p.drawPath(path);

        if (item.command.empty()) {
            continue;
        }

        const double rad = (mid) * pi / 180.0;
        const double iconRadius = (InnerRadius + OuterRadius) / 2.0;
        const QPointF pos(c.x() + iconRadius * std::cos(rad), c.y() - iconRadius * std::sin(rad));
        const QRect iconRect(
            int(std::lround(pos.x())) - iconSize / 2,
            int(std::lround(pos.y())) - iconSize / 2,
            iconSize,
            iconSize
        );
        if (!item.icon.isNull()) {
            item.icon.paint(
                &p,
                iconRect,
                Qt::AlignCenter,
                item.enabled ? QIcon::Normal : QIcon::Disabled
            );
        }
        else {
            QColor tc = (isHovered && item.enabled) ? pal.color(QPalette::HighlightedText) : text;
            if (!item.enabled) {
                tc = pal.color(QPalette::Disabled, QPalette::WindowText);
            }
            p.setPen(tc);
            QRect r = iconRect.adjusted(-12, -12, 12, 12);
            p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, item.text.left(8));
        }
    }

    // centre disc with the label of the highlighted command
    QColor centreFill = base;
    p.setPen(QPen(edge, 1.0));
    p.setBrush(centreFill);
    p.drawEllipse(inner.adjusted(6, 6, -6, -6));

    if (hovered >= 0 && !items[hovered].command.empty()) {
        // label below the ring so it is never hidden behind the cursor
        const Item& item = items[hovered];
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QString label = fm.elidedText(item.text, Qt::ElideRight, 2 * OuterRadius - 20);
        const QSize ts = fm.size(Qt::TextSingleLine, label);
        const QRectF box(
            c.x() - ts.width() / 2.0 - 10,
            c.y() + OuterRadius + 2,
            ts.width() + 20,
            ts.height() + 8
        );
        // keep within the widget
        const QRectF b = box;
        p.setPen(Qt::NoPen);
        QColor labelFill = base;
        labelFill.setAlpha(240);
        p.setBrush(labelFill);
        p.drawRoundedRect(b, 6, 6);
        p.setPen(item.enabled ? text : pal.color(QPalette::Disabled, QPalette::WindowText));
        p.drawText(b, Qt::AlignCenter, label);
    }
}

#include "moc_RadialMenu.cpp"

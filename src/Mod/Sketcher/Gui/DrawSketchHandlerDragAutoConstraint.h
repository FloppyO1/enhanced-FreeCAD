// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Turan Furkan Topak <furkan1795@gmail.com>          *
 *                                                                         *
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

#include <vector>

#include <QTimer>

#include <Mod/Sketcher/App/GeoEnum.h>

#include "DrawSketchHandler.h"

namespace Part
{
class Geometry;
}

namespace SketcherGui
{

class DrawSketchHandlerDragAutoConstraint final: public DrawSketchHandler
{
public:
    DrawSketchHandlerDragAutoConstraint();

    void mouseMove(SnapManager::SnapHandle /*snapHandle*/) override
    {}
    bool pressButton(Base::Vector2d /*pos*/) override
    {
        return false;
    }
    bool releaseButton(Base::Vector2d /*pos*/) override
    {
        return false;
    }

    std::string getToolName() const override
    {
        return "DSH_DragAutoConstraint";
    }

    QString getCrosshairCursorSVGName() const override
    {
        return QStringLiteral("Sketcher_Pointer_Create_Point");
    }

    void initDragging(const std::vector<Sketcher::GeoElementId>& dragged);

    /** Whether the dragged point snaps to the suggested constraints while dragging (see the
     * preferences), instead of suggesting them only after the mouse rests. */
    bool isSnapping(const std::vector<Sketcher::GeoElementId>& dragged) const;

    /** When snapping, seeks the constraints suggested for the dragged point and returns the
     * cursor position that moves the point onto the position satisfying them. The crosshair is
     * drawn on that position. Otherwise returns cursorPos.
     *
     * dragStart and relative are those of the drag: with a relative drag the point moves by
     * the displacement of the cursor from dragStart, otherwise it moves onto the cursor.
     */
    Base::Vector2d snap(
        const std::vector<Sketcher::GeoElementId>& dragged,
        const Base::Vector2d& cursorPos,
        const Base::Vector2d& dragStart,
        bool relative
    );
    void update(const std::vector<Sketcher::GeoElementId>& dragged, const Base::Vector2d& pos);
    void create(const std::vector<Sketcher::GeoElementId>& dragged);
    void clear();

private:
    bool canSuggestFor(const std::vector<Sketcher::GeoElementId>& dragged) const;
    void addAutoConstraint(
        Sketcher::ConstraintType type,
        int geoId,
        Sketcher::PointPos posId = Sketcher::PointPos::none
    );
    bool hasMoved(const Base::Vector2d& actualPos) const;
    Base::Vector2d getDirection(const Part::Geometry* geometry) const;
    bool isExistingConstraint(
        const Sketcher::GeoElementId& dragged,
        const AutoConstraint& constraint
    ) const;
    void removeInvalidConstraints(const Sketcher::GeoElementId& dragged);
    void onDwellTimerTimeout();
    void updateSuggestions();
    /** Suggestions for the dragged point at pointPos, among the geometry as before the drag, or
     * to the element of the 3D model if given (pointPos is then on that element). */
    void seekSnapSuggestions(
        const Sketcher::GeoElementId& dragged,
        const Base::Vector2d& pointPos,
        double snapDistance,
        const ModelReference* model = nullptr
    );
    /// Direction of the dragged line if its dragged end point were at pointPos, else zero
    Base::Vector2d getDraggedLineDirection(
        const Sketcher::GeoElementId& dragged,
        const Base::Vector2d& pointPos
    ) const;

private:
    std::vector<AutoConstraint> suggestedConstraints;
    std::vector<Sketcher::GeoElementId> draggedElements;
    Base::Vector2d startPos {0.0, 0.0};
    QTimer dwellTimer;
};

}  // namespace SketcherGui

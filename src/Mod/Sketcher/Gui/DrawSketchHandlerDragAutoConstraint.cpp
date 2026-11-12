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

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <numbers>
#include <ranges>

#include <QTimer>
#include <QWidget>

#include <App/Application.h>
#include <Precision.hxx>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Vector3D.h>
#include <Mod/Part/App/Geometry.h>
#include <Mod/Sketcher/App/Sketch.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "DrawSketchHandlerDragAutoConstraint.h"
#include "Utils.h"
#include "ViewProviderSketch.h"

using namespace SketcherGui;
using namespace Sketcher;

namespace
{
constexpr double DragAutoConstraintSnapDistanceFactor = 0.25;
constexpr int DefaultDragAutoConstraintDelay = 400;
constexpr int MaximumDragAutoConstraintDelay = 5000;

int getDragAutoConstraintDelay()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/General"
    );
    const auto delay = hGrp->GetInt("DragAutoConstraintDelay", DefaultDragAutoConstraintDelay);
    return static_cast<int>(std::clamp(delay, 0L, static_cast<long>(MaximumDragAutoConstraintDelay)));
}
}  // namespace

DrawSketchHandlerDragAutoConstraint::DrawSketchHandlerDragAutoConstraint()
{
    dwellTimer.setSingleShot(true);
    QObject::connect(&dwellTimer, &QTimer::timeout, &dwellTimer, [this]() { onDwellTimerTimeout(); });
}

bool DrawSketchHandlerDragAutoConstraint::canSuggestFor(const std::vector<GeoElementId>& dragged) const
{
    return sketchgui && sketchgui->Autoconstraints.getValue() && dragged.size() == 1
        && dragged.front().Pos != PointPos::none;
}

void DrawSketchHandlerDragAutoConstraint::initDragging(const std::vector<GeoElementId>& dragged)
{
    clear();

    if (QWidget* widget = getCursorWidget()) {
        oldCursor = widget->cursor();
    }

    SketchObject* obj = getSketchObject();
    if (!obj || !canSuggestFor(dragged)) {
        return;
    }

    updateCursor();
    // Restore the previous cursor, keeping the initialized pixmap for auto-constraint hints.
    unsetCursor();
    startPos = toVector2d(obj->getPoint(dragged.front().GeoId, dragged.front().Pos));
}

void DrawSketchHandlerDragAutoConstraint::addAutoConstraint(ConstraintType type, int geoId, PointPos posId)
{
    AutoConstraint constr;
    constr.Type = type;
    constr.GeoId = geoId;
    constr.PosId = posId;
    suggestedConstraints.push_back(constr);
}

void DrawSketchHandlerDragAutoConstraint::clear()
{
    dwellTimer.stop();
    suggestedConstraints.clear();
    draggedElements.clear();

    // Remove the crosshair drawn on the snapped position, if any
    resetSuggestedConstraintSnap();
    renderSuggestedConstraintSnapCursor(startPos);

    unsetCursor();
}

bool DrawSketchHandlerDragAutoConstraint::isSnapping(const std::vector<GeoElementId>& dragged) const
{
    return canSuggestFor(dragged) && getSuggestedConstraintSnapDistance() > 0.0;
}

Base::Vector2d DrawSketchHandlerDragAutoConstraint::getDraggedLineDirection(
    const GeoElementId& dragged,
    const Base::Vector2d& pointPos
) const
{
    SketchObject* obj = sketchgui->getSketchObject();
    const Part::Geometry* geometry = obj ? obj->getGeometry(dragged.GeoId) : nullptr;
    if (!geometry || !geometry->is<Part::GeomLineSegment>()
        || (dragged.Pos != PointPos::start && dragged.Pos != PointPos::end)) {
        return Base::Vector2d(0.0, 0.0);
    }

    const auto* line = static_cast<const Part::GeomLineSegment*>(geometry);
    const Base::Vector2d otherEnd = toVector2d(
        dragged.Pos == PointPos::start ? line->getEndPoint() : line->getStartPoint()
    );
    return pointPos - otherEnd;
}

void DrawSketchHandlerDragAutoConstraint::seekSnapSuggestions(
    const GeoElementId& dragged,
    const Base::Vector2d& pointPos,
    double snapDistance,
    const ModelReference* model
)
{
    suggestedConstraints.clear();

    SketchObject* obj = sketchgui->getSketchObject();
    if (!obj) {
        return;
    }

    if (model) {
        // The model element is imported as external geometry when the constraint is created
        AutoConstraint constr;
        constr.Type = None;
        constr.GeoId = GeoEnum::GeoUndef;
        constr.PosId = model->posId;
        constr.ExternalObjectName = model->objectName;
        constr.ExternalSubName = model->subName;
        switch (model->kind) {
            case ModelReference::Kind::Point:
                constr.Type = Coincident;
                break;
            case ModelReference::Kind::Edge:
                constr.Type = PointOnObject;
                break;
            case ModelReference::Kind::EdgeMidpoint:
                constr.Type = Symmetric;
                break;
            default:
                break;
        }
        if (constr.Type != None) {
            suggestedConstraints.push_back(constr);
        }
    }

    // Candidates already constrained to the dragged point move with it: they are no targets
    auto isCandidate = [&](ConstraintType type, int geoId, PointPos posId) {
        AutoConstraint candidate;
        candidate.Type = type;
        candidate.GeoId = geoId;
        candidate.PosId = posId;
        return geoId != dragged.GeoId && !isExistingConstraint(dragged, candidate);
    };

    // Vertices first, including the origin
    int bestGeoId = GeoEnum::GeoUndef;
    if (model) {
        snapDistance = 0.0;  // The position is fixed by the model element
    }
    PointPos bestPosId = PointPos::none;
    double bestDistance = snapDistance;

    if (pointPos.Length() < bestDistance && isCandidate(Coincident, GeoEnum::RtPnt, PointPos::start)) {
        bestDistance = pointPos.Length();
        bestGeoId = GeoEnum::RtPnt;
        bestPosId = PointPos::start;
    }

    for (int vertexIndex = 0; vertexIndex <= obj->getHighestVertexIndex(); ++vertexIndex) {
        int geoId = GeoEnum::GeoUndef;
        PointPos posId = PointPos::none;
        obj->getGeoVertexIndex(vertexIndex, geoId, posId);
        if (geoId == GeoEnum::GeoUndef) {
            continue;
        }

        const double distance = (pointPos - toVector2d(obj->getPoint(geoId, posId))).Length();
        if (distance < bestDistance && isCandidate(Coincident, geoId, posId)) {
            bestDistance = distance;
            bestGeoId = geoId;
            bestPosId = posId;
        }
    }

    if (bestGeoId != GeoEnum::GeoUndef) {
        addAutoConstraint(Coincident, bestGeoId, bestPosId);
    }
    else {
        // Curves, including the axes
        bool lineCenter = false;

        auto considerCurve = [&](int geoId, const Base::Vector2d& closest, bool isLineCenter) {
            const double distance = (pointPos - closest).Length();
            if (distance < bestDistance && isCandidate(PointOnObject, geoId, PointPos::none)) {
                bestDistance = distance;
                bestGeoId = geoId;
                lineCenter = isLineCenter;
            }
        };

        considerCurve(GeoEnum::HAxis, Base::Vector2d(pointPos.x, 0.0), false);
        considerCurve(GeoEnum::VAxis, Base::Vector2d(0.0, pointPos.y), false);

        auto considerGeometry = [&](int geoId) {
            const Part::Geometry* geo = obj->getGeometry(geoId);
            if (const auto* line = dynamic_cast<const Part::GeomLineSegment*>(geo)) {
                const Base::Vector2d a = toVector2d(line->getStartPoint());
                const Base::Vector2d ab = toVector2d(line->getEndPoint()) - a;
                const double len2 = ab * ab;
                if (len2 <= Precision::SquareConfusion()) {
                    return;
                }
                const double t = std::clamp(((pointPos - a) * ab) / len2, 0.0, 1.0);
                const Base::Vector2d midpoint = a + ab * 0.5;
                const bool isLineCenter = (pointPos - midpoint).Length() < ab.Length() * 0.05;
                considerCurve(geoId, a + ab * t, isLineCenter);
            }
            else if (const auto* curve = dynamic_cast<const Part::GeomCurve*>(geo)) {
                try {
                    double parameter = 0.0;
                    if (curve->closestParameter(toVector3d(pointPos), parameter)) {
                        considerCurve(geoId, toVector2d(curve->pointAtParameter(parameter)), false);
                    }
                }
                catch (const Base::Exception&) {
                }
            }
        };

        for (int geoId = 0; geoId <= obj->getHighestCurveIndex(); ++geoId) {
            considerGeometry(geoId);
        }
        for (int geoId = GeoEnum::RefExt; geoId >= -obj->getExternalGeometryCount(); --geoId) {
            considerGeometry(geoId);
        }

        if (bestGeoId != GeoEnum::GeoUndef) {
            addAutoConstraint(lineCenter ? Symmetric : PointOnObject, bestGeoId);
        }
    }

    // Horizontal or vertical dragged line
    const Base::Vector2d dir = getDraggedLineDirection(dragged, pointPos);
    if (isOffsetAlongDirection(dir, Base::Vector2d(1.0, 0.0))) {
        addAutoConstraint(Horizontal, GeoEnum::GeoUndef);
    }
    else if (isOffsetAlongDirection(dir, Base::Vector2d(0.0, 1.0))) {
        addAutoConstraint(Vertical, GeoEnum::GeoUndef);
    }

    removeInvalidConstraints(dragged);
}

Base::Vector2d DrawSketchHandlerDragAutoConstraint::snap(
    const std::vector<GeoElementId>& dragged,
    const Base::Vector2d& cursorPos,
    const Base::Vector2d& dragStart,
    bool relative
)
{
    if (!isSnapping(dragged)) {
        return cursorPos;
    }

    dwellTimer.stop();
    draggedElements = dragged;
    const GeoElementId& draggedPoint = dragged.front();
    const double snapDistance = getSuggestedConstraintSnapDistance();

    // Position of the dragged point for a cursor position, and conversely
    auto toPoint = [&](const Base::Vector2d& cursor) {
        return relative ? startPos + (cursor - dragStart) : cursor;
    };
    auto toCursor = [&](const Base::Vector2d& point) {
        return relative ? dragStart + (point - startPos) : point;
    };

    Base::Vector2d pointPos = toPoint(cursorPos);
    if (!hasMoved(pointPos)) {
        clear();
        draggedElements = dragged;
        return cursorPos;
    }

    // Element of the 3D model the cursor is snapped to. As when drawing, a vertex of the sketch
    // nearby has precedence over an edge of the model.
    const ModelReference& model = getSnappedModelReference();
    if (model.isValid() && (cursorPos - model.position).Length() <= Precision::Confusion()) {
        seekSnapSuggestions(draggedPoint, model.position, snapDistance);
        const bool nearSketchVertex = std::ranges::any_of(
            suggestedConstraints,
            [](const AutoConstraint& ac) { return ac.Type == Coincident; }
        );

        if (model.kind == ModelReference::Kind::Point || !nearSketchVertex) {
            pointPos = model.position;
            seekSnapSuggestions(draggedPoint, pointPos, snapDistance, &model);
            updateSuggestedConstraintSnap(
                suggestedConstraints,
                pointPos,
                getDraggedLineDirection(draggedPoint, pointPos)
            );
            renderSuggestConstraintsCursor(suggestedConstraints);
            renderSuggestedConstraintSnapCursor(pointPos);
            return toCursor(pointPos);
        }
        pointPos = model.position;
    }

    seekSnapSuggestions(draggedPoint, pointPos, snapDistance);
    updateSuggestedConstraintSnap(
        suggestedConstraints,
        pointPos,
        getDraggedLineDirection(draggedPoint, pointPos)
    );

    Base::Vector2d snapPos;
    if (getSuggestedConstraintSnapPoint(snapPos)) {
        // The constraints as suggested where the point is snapped to
        pointPos = snapPos;
        seekSnapSuggestions(draggedPoint, pointPos, snapDistance);
        updateSuggestedConstraintSnap(
            suggestedConstraints,
            pointPos,
            getDraggedLineDirection(draggedPoint, pointPos)
        );
    }

    if (suggestedConstraints.empty()) {
        resetSuggestedConstraintSnap();
        renderSuggestedConstraintSnapCursor(pointPos);
        unsetCursor();
    }
    else {
        renderSuggestConstraintsCursor(suggestedConstraints);
        renderSuggestedConstraintSnapCursor(pointPos);
    }

    return toCursor(pointPos);
}

bool DrawSketchHandlerDragAutoConstraint::hasMoved(const Base::Vector2d& actualPos) const
{
    return (actualPos - startPos).Sqr() > Precision::SquareConfusion();
}

Base::Vector2d DrawSketchHandlerDragAutoConstraint::getDirection(const Part::Geometry* geometry) const
{
    if (!geometry || !geometry->isDerivedFrom<Part::GeomLineSegment>()) {
        return Base::Vector2d(0.0, 0.0);
    }

    const auto* line = static_cast<const Part::GeomLineSegment*>(geometry);
    return toVector2d(line->getEndPoint()) - toVector2d(line->getStartPoint());
}

bool DrawSketchHandlerDragAutoConstraint::isExistingConstraint(
    const GeoElementId& dragged,
    const AutoConstraint& constraint
) const
{
    auto samePoint = [](int geoId1, PointPos posId1, int geoId2, PointPos posId2) {
        return geoId1 == geoId2 && posId1 == posId2;
    };

    auto isDraggedPoint = [&](int geoId, PointPos posId) {
        return samePoint(geoId, posId, dragged.GeoId, dragged.Pos);
    };

    const auto& constraints = sketchgui->getSketchObject()->Constraints.getValues();
    for (const Constraint* c : constraints) {
        if (!c) {
            continue;
        }

        switch (constraint.Type) {
            case Coincident:
                if (c->Type == Coincident
                    && ((isDraggedPoint(c->First, c->FirstPos)
                         && samePoint(c->Second, c->SecondPos, constraint.GeoId, constraint.PosId))
                        || (isDraggedPoint(c->Second, c->SecondPos)
                            && samePoint(c->First, c->FirstPos, constraint.GeoId, constraint.PosId)))) {
                    return true;
                }
                break;

            case PointOnObject:
                if (c->Type == PointOnObject && isDraggedPoint(c->First, c->FirstPos)
                    && c->Second == constraint.GeoId) {
                    return true;
                }
                break;

            case Symmetric: {
                if (c->Type != Symmetric || !isDraggedPoint(c->Third, c->ThirdPos)) {
                    break;
                }

                const bool sameLineStartEnd
                    = samePoint(c->First, c->FirstPos, constraint.GeoId, PointPos::start)
                    && samePoint(c->Second, c->SecondPos, constraint.GeoId, PointPos::end);
                const bool sameLineEndStart
                    = samePoint(c->First, c->FirstPos, constraint.GeoId, PointPos::end)
                    && samePoint(c->Second, c->SecondPos, constraint.GeoId, PointPos::start);

                if (sameLineStartEnd || sameLineEndStart) {
                    return true;
                }
                break;
            }

            case Horizontal:
            case Vertical: {
                const int targetGeoId = constraint.GeoId != GeoEnum::GeoUndef ? constraint.GeoId
                                                                              : dragged.GeoId;
                if (c->Type == constraint.Type && c->First == targetGeoId) {
                    return true;
                }
                break;
            }

            default:
                break;
        }
    }

    return false;
}

void DrawSketchHandlerDragAutoConstraint::removeInvalidConstraints(const GeoElementId& dragged)
{
    suggestedConstraints.erase(
        std::remove_if(
            suggestedConstraints.begin(),
            suggestedConstraints.end(),
            [this, &dragged](const AutoConstraint& constraint) {
                const bool isSelfPoint = constraint.Type == Coincident
                    && constraint.GeoId == dragged.GeoId && constraint.PosId == dragged.Pos;
                const bool isSelfObject = constraint.Type == PointOnObject
                    && constraint.GeoId == dragged.GeoId;

                return isSelfPoint || isSelfObject || isExistingConstraint(dragged, constraint);
            }
        ),
        suggestedConstraints.end()
    );
}

void DrawSketchHandlerDragAutoConstraint::update(
    const std::vector<GeoElementId>& draggedElements,
    const Base::Vector2d& /*pos*/
)
{
    dwellTimer.stop();
    if (isSnapping(draggedElements)) {
        this->draggedElements = draggedElements;
        return;  // The suggestions follow the dragged point, see snap()
    }

    suggestedConstraints.clear();
    unsetCursor();

    this->draggedElements = draggedElements;

    if (!canSuggestFor(this->draggedElements)) {
        return;
    }

    dwellTimer.start(getDragAutoConstraintDelay());
}

void DrawSketchHandlerDragAutoConstraint::onDwellTimerTimeout()
{
    try {
        updateSuggestions();
    }
    catch (const Base::Exception& e) {
        clear();
        Base::Console().error("Failed to update drag auto-constraints: {}\n", e.what());
    }
    catch (const std::exception& e) {
        clear();
        Base::Console().error("C++ exception while updating drag auto-constraints: {}\n", e.what());
    }
    catch (...) {
        clear();
        Base::Console().error("Unknown exception while updating drag auto-constraints\n");
    }
}

void DrawSketchHandlerDragAutoConstraint::updateSuggestions()
{
    suggestedConstraints.clear();

    SketchObject* obj = getSketchObject();
    if (!obj || !canSuggestFor(draggedElements)) {
        unsetCursor();
        return;
    }

    const auto& dragged = draggedElements.front();
    const Sketch& solvedSketch = sketchgui->getSolvedSketch();

    std::vector<std::unique_ptr<Part::Geometry>> solvedGeometry;
    for (Part::Geometry* geometry : solvedSketch.extractGeometry(true, false)) {
        solvedGeometry.emplace_back(geometry);
    }

    auto getSolvedGeometry = [&solvedGeometry](int geoId) -> const Part::Geometry* {
        if (geoId < 0 || static_cast<std::size_t>(geoId) >= solvedGeometry.size()) {
            return nullptr;
        }
        return solvedGeometry[geoId].get();
    };

    const Base::Vector2d actualPos = toVector2d(solvedSketch.getPoint(dragged.GeoId, dragged.Pos));

    if (!hasMoved(actualPos)) {
        unsetCursor();
        return;
    }

    const double snapDistance = std::max(
        Precision::Confusion(),
        DragAutoConstraintSnapDistanceFactor * sketchgui->getScaleFactor()
    );

    auto isDraggedPoint = [&dragged](int geoId, PointPos posId) {
        return dragged.GeoId == geoId && dragged.Pos == posId;
    };

    int bestPointGeoId = GeoEnum::GeoUndef;
    PointPos bestPointPos = PointPos::none;
    double bestPointDist = snapDistance;

    const int highestVertex = obj->getHighestVertexIndex();
    for (int vertexIndex = 0; vertexIndex <= highestVertex; ++vertexIndex) {
        int geoId = GeoEnum::GeoUndef;
        PointPos posId = PointPos::none;
        obj->getGeoVertexIndex(vertexIndex, geoId, posId);

        if (geoId == GeoEnum::GeoUndef || isDraggedPoint(geoId, posId)) {
            continue;
        }

        const double dist = (actualPos - toVector2d(solvedSketch.getPoint(geoId, posId))).Length();
        if (dist < bestPointDist) {
            bestPointDist = dist;
            bestPointGeoId = geoId;
            bestPointPos = posId;
        }
    }

    if (bestPointGeoId != GeoEnum::GeoUndef) {
        addAutoConstraint(Coincident, bestPointGeoId, bestPointPos);
    }
    else if (actualPos.Length() < snapDistance) {
        addAutoConstraint(Coincident, GeoEnum::RtPnt, PointPos::start);
    }
    else {
        struct CurveCandidate
        {
            int geoId = GeoEnum::GeoUndef;
            double distance = std::numeric_limits<double>::max();
            bool lineCenter = false;
        };

        CurveCandidate bestCurve;

        auto considerCurve = [&](int geoId, double distance, bool lineCenter = false) {
            if (distance >= snapDistance || distance >= bestCurve.distance) {
                return;
            }

            bestCurve.geoId = geoId;
            bestCurve.distance = distance;
            bestCurve.lineCenter = lineCenter;
        };

        for (int geoId = 0; geoId <= obj->getHighestCurveIndex(); ++geoId) {
            const Part::Geometry* geo = getSolvedGeometry(geoId);
            if (!geo || geoId == dragged.GeoId) {
                continue;
            }

            if (geo->is<Part::GeomLineSegment>()) {
                const auto* line = static_cast<const Part::GeomLineSegment*>(geo);

                const Base::Vector2d a = toVector2d(line->getStartPoint());
                const Base::Vector2d b = toVector2d(line->getEndPoint());
                const Base::Vector2d ab = b - a;
                const double len2 = ab * ab;

                if (len2 <= Precision::SquareConfusion()) {
                    continue;
                }

                double t = ((actualPos - a) * ab) / len2;
                t = std::max(0.0, std::min(1.0, t));

                const Base::Vector2d projection = a + ab * t;
                const double distance = (actualPos - projection).Length();
                const Base::Vector2d midpoint = (a + b) / 2.0;
                const bool lineCenter = (actualPos - midpoint).Length() < ab.Length() * 0.05;
                considerCurve(geoId, distance, lineCenter);
            }
            else if (geo->isDerivedFrom<Part::GeomCurve>()) {
                const auto* curve = static_cast<const Part::GeomCurve*>(geo);
                double parameter;

                try {
                    if (curve->closestParameter(toVector3d(actualPos), parameter)) {
                        const Base::Vector2d closestPoint = toVector2d(
                            curve->pointAtParameter(parameter)
                        );
                        considerCurve(geoId, (actualPos - closestPoint).Length());
                    }
                }
                catch (const Base::CADKernelError&) {
                    continue;
                }
            }
        }

        if (bestCurve.geoId != GeoEnum::GeoUndef) {
            addAutoConstraint(bestCurve.lineCenter ? Symmetric : PointOnObject, bestCurve.geoId);
        }
        else if (std::abs(actualPos.y) < snapDistance) {
            addAutoConstraint(PointOnObject, GeoEnum::HAxis);
        }
        else if (std::abs(actualPos.x) < snapDistance) {
            addAutoConstraint(PointOnObject, GeoEnum::VAxis);
        }
    }

    const Base::Vector2d dir = getDirection(getSolvedGeometry(dragged.GeoId));
    if (dir.Sqr() > Precision::SquareConfusion()) {
        using std::numbers::pi;
        constexpr double angleDevRad = Base::toRadians<double>(2);

        AutoConstraint constr;
        constr.Type = None;
        constr.GeoId = GeoEnum::GeoUndef;
        constr.PosId = PointPos::none;

        const double angle = std::abs(atan2(dir.y, dir.x));
        if (angle < angleDevRad || (pi - angle) < angleDevRad) {
            constr.Type = Horizontal;
        }
        else if (std::abs(angle - pi / 2) < angleDevRad) {
            constr.Type = Vertical;
        }

        if (constr.Type != None) {
            suggestedConstraints.push_back(constr);
        }
    }

    removeInvalidConstraints(dragged);

    if (suggestedConstraints.empty()) {
        unsetCursor();
    }
    else {
        renderSuggestConstraintsCursor(suggestedConstraints);
    }
}

void DrawSketchHandlerDragAutoConstraint::create(const std::vector<GeoElementId>& dragged)
{
    if (!canSuggestFor(dragged) || suggestedConstraints.empty()) {
        return;
    }

    SketchObject* obj = getSketchObject();
    if (!obj) {
        return;
    }

    // Import the elements of the 3D model the suggestions refer to, within the drag command
    resolvePendingExternalAutoConstraints(suggestedConstraints);

    std::vector<std::unique_ptr<Constraint>> autoConstraints;

    for (const AutoConstraint& suggestion : suggestedConstraints) {
        if (!generateOneAutoConstraintFromSuggestion(
                suggestion,
                dragged.front().GeoId,
                dragged.front().Pos,
                autoConstraints
            )) {
            break;
        }
    }

    const bool valid = filterRedundantAutoConstraints(autoConstraints);
    obj->solve(false);

    if (!valid) {
        return;
    }

    addGeneratedAutoConstraints(autoConstraints);
}

// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 Pierre-Louis Boyer <pierrelouis.boyer@gmail.com>   *
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

#include <QApplication>

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>

#include <Inventor/SoPickedPoint.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/lists/SoPickedPointList.h>

#include <App/Document.h>
#include <App/GeoFeature.h>
#include <Base/Tools.h>
#include <Gui/Document.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "SnapManager.h"
#include "ViewProviderSketch.h"


using namespace SketcherGui;
using namespace Sketcher;

namespace
{

/// Distance (relative to the view scale factor) below which the cursor snaps to a circle center
constexpr double modelCenterSnapScale = 0.2;
/// Relative distance from the middle of a straight edge below which the cursor snaps to it
constexpr double modelEdgeMidpointRatio = 0.05;

bool isVertexName(const std::string& element)
{
    return element.starts_with("Vertex") && element.size() > 6;
}

bool isEdgeName(const std::string& element)
{
    return element.starts_with("Edge") && element.size() > 4;
}

}  // namespace

/************************************ Attorney *******************************************/

inline int ViewProviderSketchSnapAttorney::getPreselectPoint(const ViewProviderSketch& vp)
{
    return vp.getPreselectPoint();
}

inline int ViewProviderSketchSnapAttorney::getPreselectCross(const ViewProviderSketch& vp)
{
    return vp.getPreselectCross();
}

inline int ViewProviderSketchSnapAttorney::getPreselectCurve(const ViewProviderSketch& vp)
{
    return vp.getPreselectCurve();
}

/**************************** ParameterObserver nested class *****************************/
SnapManager::ParameterObserver::ParameterObserver(SnapManager& client)
    : client(client)
{
    initParameters();
    subscribeToParameters();
}

SnapManager::ParameterObserver::~ParameterObserver()
{
    unsubscribeToParameters();
}

void SnapManager::ParameterObserver::initParameters()
{
    // static map to avoid substantial if/else branching
    //
    // key->first               => String of parameter,
    // key->second              => Update function to be called for the parameter,
    str2updatefunction = {
        {"Snap", [this](const std::string& param) { updateSnapParameter(param); }},
        {"SnapToObjects", [this](const std::string& param) { updateSnapToObjectParameter(param); }},
        {"SnapToModel", [this](const std::string& param) { updateSnapToModelParameter(param); }},
        {"SnapToGrid", [this](const std::string& param) { updateSnapToGridParameter(param); }},
        {"SnapAngle", [this](const std::string& param) { updateSnapAngleParameter(param); }},
    };

    for (auto& val : str2updatefunction) {
        auto string = val.first;
        auto function = val.second;

        function(string);
    }
}

void SnapManager::ParameterObserver::updateSnapParameter(const std::string& parametername)
{
    ParameterGrp::handle hGrp = getParameterGrpHandle();

    client.snapRequested = hGrp->GetBool(parametername.c_str(), true);
}

void SnapManager::ParameterObserver::updateSnapToObjectParameter(const std::string& parametername)
{
    ParameterGrp::handle hGrp = getParameterGrpHandle();

    client.snapToObjectsRequested = hGrp->GetBool(parametername.c_str(), true);
}

void SnapManager::ParameterObserver::updateSnapToModelParameter(const std::string& parametername)
{
    ParameterGrp::handle hGrp = getParameterGrpHandle();

    client.snapToModelRequested = hGrp->GetBool(parametername.c_str(), true);
}

void SnapManager::ParameterObserver::updateSnapToGridParameter(const std::string& parametername)
{
    ParameterGrp::handle hGrp = getParameterGrpHandle();

    client.snapToGridRequested = hGrp->GetBool(parametername.c_str(), false);
}

void SnapManager::ParameterObserver::updateSnapAngleParameter(const std::string& parametername)
{
    ParameterGrp::handle hGrp = getParameterGrpHandle();

    client.snapAngle
        = fmod(Base::toRadians(hGrp->GetFloat(parametername.c_str(), 5.)), 2 * std::numbers::pi);
}

void SnapManager::ParameterObserver::subscribeToParameters()
{
    try {
        ParameterGrp::handle hGrp = getParameterGrpHandle();
        hGrp->Attach(this);
    }
    catch (const Base::ValueError& e) {  // ensure that if parameter strings are not well-formed,
                                         // the exception is not propagated
        Base::Console().developerError("SnapManager", "Malformed parameter string: {}\n", e.what());
    }
}

void SnapManager::ParameterObserver::unsubscribeToParameters()
{
    try {
        ParameterGrp::handle hGrp = getParameterGrpHandle();
        hGrp->Detach(this);
    }
    catch (const Base::ValueError& e) {  // ensure that if parameter strings are not well-formed,
                                         // the program is not terminated when calling the noexcept
                                         // destructor.
        Base::Console().developerError("SnapManager", "Malformed parameter string: {}\n", e.what());
    }
}

void SnapManager::ParameterObserver::OnChange(Base::Subject<const char*>& rCaller, const char* sReason)
{
    (void)rCaller;

    auto key = str2updatefunction.find(sReason);
    if (key != str2updatefunction.end()) {
        auto string = key->first;
        auto function = key->second;

        function(string);
    }
}

ParameterGrp::handle SnapManager::ParameterObserver::getParameterGrpHandle()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/Snap"
    );
}

//**************************** SnapManager class ******************************

SnapManager::SnapManager(ViewProviderSketch& vp)
    : viewProvider(vp)
    , angleSnapRequested(false)
    , referencePoint(Base::Vector2d(0., 0.))
    , lastMouseAngle(0.0)
{
    // Create parameter observer and initialise watched parameters
    pObserver = std::make_unique<SnapManager::ParameterObserver>(*this);
}

SnapManager::~SnapManager()
{}

Base::Vector2d SnapManager::snap(Base::Vector2d inputPos, SnapType mask)
{
    modelReference = ModelReference();

    if (!snapRequested) {
        return inputPos;
    }

    Base::Vector2d snapPos = inputPos;

    // In order of priority:

    // 1 - Snap at an angle
    if ((static_cast<int>(mask) & static_cast<int>(SnapType::Angle)) && angleSnapRequested
        && QApplication::keyboardModifiers() == Qt::ControlModifier
        && snapAtAngle(inputPos, snapPos)) {
        return snapPos;
    }
    else {
        lastMouseAngle = 0.0;
    }

    // 2 - Snap to objects (may partially snap to axis, leaving other coordinate for grid)
    bool snappedToObject = false;
    if ((static_cast<int>(mask)
         & (static_cast<int>(SnapType::Point) | static_cast<int>(SnapType::Edge)))
        && snapToObjectsRequested) {
        snappedToObject = snapToObject(inputPos, snapPos, mask);
        if (snappedToObject) {
            return snapPos;  // Full snap (point or curve) - done
        }
        // if false was returned but snapPos was modified (axis case), continue to grid snap

        // Geometry of the 3D model, only when nothing of the sketch itself is preselected
        if (snapPos == inputPos && (static_cast<int>(mask) & static_cast<int>(SnapType::Point))
            && (static_cast<int>(mask) & static_cast<int>(SnapType::Edge))
            && snapToModel(inputPos, snapPos)) {
            return snapPos;
        }
    }

    // 3 - Snap to grid (will work on coordinates not already locked by axis snap)
    if ((static_cast<int>(mask) & static_cast<int>(SnapType::Grid)) && snapToGridRequested
        /*&& viewProvider.ShowGrid.getValue() */) {  // Snap to grid is enabled
                                                     // even if the grid is not visible.

        // use snapPos as input (which may have one coordinate locked by axis)
        Base::Vector2d gridSnapResult = snapPos;
        if (snapToGrid(snapPos, gridSnapResult)) {
            return gridSnapResult;
        }
        // if grid snap happened, return the result which combines axis + grid
        // if axis locked a coordinate, snapPos already had it, and grid snap respected it
        return snapPos;
    }

    return snapPos;
}

bool SnapManager::snapAtAngle(Base::Vector2d inputPos, Base::Vector2d& snapPos)
{
    double length = (inputPos - referencePoint).Length();

    double angle1 = (inputPos - referencePoint).Angle();
    double angle2 = angle1 + (angle1 < 0. ? 2 : -2) * std::numbers::pi;
    lastMouseAngle = abs(angle1 - lastMouseAngle) < abs(angle2 - lastMouseAngle) ? angle1 : angle2;

    double angle = round(lastMouseAngle / snapAngle) * snapAngle;
    snapPos = referencePoint + length * Base::Vector2d(cos(angle), sin(angle));

    return true;
}

bool SnapManager::snapToObject(Base::Vector2d inputPos, Base::Vector2d& snapPos, SnapType mask)
{
    Sketcher::SketchObject* Obj = viewProvider.getSketchObject();
    int geoId = GeoEnum::GeoUndef;
    Sketcher::PointPos posId = Sketcher::PointPos::none;

    int VtId = ViewProviderSketchSnapAttorney::getPreselectPoint(viewProvider);
    int CrsId = ViewProviderSketchSnapAttorney::getPreselectCross(viewProvider);
    int CrvId = ViewProviderSketchSnapAttorney::getPreselectCurve(viewProvider);

    if ((static_cast<int>(mask) & static_cast<int>(SnapType::Point)) && (CrsId == 0 || VtId >= 0)) {
        if (CrsId == 0) {
            geoId = Sketcher::GeoEnum::RtPnt;
            posId = Sketcher::PointPos::start;
        }
        else if (VtId >= 0) {
            Obj->getGeoVertexIndex(VtId, geoId, posId);
        }

        snapPos.x = Obj->getPoint(geoId, posId).x;
        snapPos.y = Obj->getPoint(geoId, posId).y;
        return true;
    }
    else if (static_cast<int>(mask) & static_cast<int>(SnapType::Edge)) {
        if (CrsId == 1) {  // H_Axis
            snapPos.y = 0;
            // dont return true, allow grid snap to handle X coordinate
            return false;
        }
        else if (CrsId == 2) {  // V_Axis
            snapPos.x = 0;
            // dont return true, allow grid snap to handle Y coordinate
            return false;
        }
        else if (CrvId >= 0 || CrvId <= Sketcher::GeoEnum::RefExt) {  // Curves

            const Part::Geometry* geo = Obj->getGeometry(CrvId);

            Base::Vector3d pointToOverride(inputPos.x, inputPos.y, 0.);

            double pointParam = 0.0;
            auto curve = dynamic_cast<const Part::GeomCurve*>(geo);
            if (curve) {
                try {
                    curve->closestParameter(pointToOverride, pointParam);
                    pointToOverride = curve->pointAtParameter(pointParam);
                }
                catch (Base::CADKernelError& e) {
                    e.reportException();
                    return false;
                }

                // If it is a line, then we check if we need to snap to the middle.
                if (geo->is<Part::GeomLineSegment>()) {
                    const Part::GeomLineSegment* line = static_cast<const Part::GeomLineSegment*>(geo);
                    snapToLineMiddle(pointToOverride, line);
                }

                // If it is an arc, then we check if we need to snap to the middle (not the center).
                if (geo->is<Part::GeomArcOfCircle>()) {
                    const Part::GeomArcOfCircle* arc = static_cast<const Part::GeomArcOfCircle*>(geo);
                    snapToArcMiddle(pointToOverride, arc);
                }

                snapPos.x = pointToOverride.x;
                snapPos.y = pointToOverride.y;

                return true;
            }
        }
    }
    return false;
}

bool SnapManager::snapToModel(Base::Vector2d inputPos, Base::Vector2d& snapPos)
{
    // Only while a tool creates geometry: only tools create the matching constraints
    if (!snapToModelRequested
        || viewProvider.getSketchMode() != ViewProviderSketch::STATUS_SKETCH_UseHandler) {
        acquiredCenter = ModelReference();
        return false;
    }

    // Anything of the sketch itself under the cursor has precedence
    if (ViewProviderSketchSnapAttorney::getPreselectPoint(viewProvider) >= 0
        || ViewProviderSketchSnapAttorney::getPreselectCross(viewProvider) >= 0
        || ViewProviderSketchSnapAttorney::getPreselectCurve(viewProvider) >= 0
        || ViewProviderSketchSnapAttorney::getPreselectCurve(viewProvider) <= GeoEnum::RefExt) {
        return false;
    }

    Sketcher::SketchObject* sketch = viewProvider.getSketchObject();
    const Base::Placement inverseSketchPlacement = sketch->Placement.getValue().inverse();
    auto toSketch = [&inverseSketchPlacement](const gp_Pnt& point) {
        Base::Vector3d vector(point.X(), point.Y(), point.Z());
        inverseSketchPlacement.multVec(vector, vector);
        return Base::Vector2d(vector.x, vector.y);
    };

    ModelReference result;

    const std::string& element = modelPick.element;
    App::DocumentObject* object = modelPick.objectName.empty()
        ? nullptr
        : sketch->getDocument()->getObject(modelPick.objectName.c_str());
    if (object && object != sketch && sketch->isExternalAllowed(object->getDocument(), object)) {
        try {
            // Same shape and coordinate system as SketchObject::addExternal()
            const Part::TopoShape shape = Part::Feature::getTopoShape(
                object,
                Part::ShapeOption::ResolveLink | Part::ShapeOption::Transform
            );
            const Part::TopoShape subShape = shape.getSubTopoShape(element.c_str(), /*silent*/ true);

            result.objectName = object->getNameInDocument();
            result.subName = element;

            if (!subShape.isNull() && subShape.shapeType(/*silent*/ true) == TopAbs_VERTEX) {
                result.kind = ModelReference::Kind::Point;
                result.posId = PointPos::start;
                result.position = toSketch(BRep_Tool::Pnt(TopoDS::Vertex(subShape.getShape())));
            }
            else if (!subShape.isNull() && subShape.shapeType(/*silent*/ true) == TopAbs_EDGE) {
                BRepAdaptor_Curve curve(TopoDS::Edge(subShape.getShape()));

                Base::Vector3d sketchNormal(0., 0., 1.);
                sketch->Placement.getValue().getRotation().multVec(sketchNormal, sketchNormal);

                if (curve.GetType() == GeomAbs_Line) {
                    const Base::Vector2d start = toSketch(curve.Value(curve.FirstParameter()));
                    const Base::Vector2d end = toSketch(curve.Value(curve.LastParameter()));
                    const Base::Vector2d direction = end - start;
                    const double length = direction.Length();

                    if (length < Precision::Confusion()) {
                        // Edge perpendicular to the sketch: it is projected as a point
                        result.kind = ModelReference::Kind::Point;
                        result.posId = PointPos::start;
                        result.position = start;
                    }
                    else {
                        const double parameter
                            = std::clamp(((inputPos - start) * direction) / (length * length), 0., 1.);
                        const Base::Vector2d onEdge = start + direction * parameter;
                        const Base::Vector2d middle = (start + end) * 0.5;

                        if ((onEdge - middle).Length() < length * modelEdgeMidpointRatio) {
                            result.kind = ModelReference::Kind::EdgeMidpoint;
                            result.position = middle;
                        }
                        else {
                            result.kind = ModelReference::Kind::Edge;
                            result.position = onEdge;
                        }
                    }
                }
                else if (curve.GetType() == GeomAbs_Circle) {
                    const gp_Circ circle = curve.Circle();
                    const gp_Dir& axis = circle.Axis().Direction();
                    const double cosAngle = axis.X() * sketchNormal.x + axis.Y() * sketchNormal.y
                        + axis.Z() * sketchNormal.z;

                    // Only circles parallel to the sketch are projected as circles (with a center)
                    if (std::fabs(std::fabs(cosAngle) - 1.) < Precision::Angular()) {
                        const Base::Vector2d center = toSketch(circle.Location());

                        acquiredCenter.kind = ModelReference::Kind::Point;
                        acquiredCenter.objectName = result.objectName;
                        acquiredCenter.subName = result.subName;
                        acquiredCenter.position = center;
                        acquiredCenter.posId = PointPos::mid;

                        Base::Vector2d radial = inputPos - center;
                        if (radial.Length() > Precision::Confusion()) {
                            result.kind = ModelReference::Kind::Edge;
                            result.position = center + radial.Normalize() * circle.Radius();
                        }
                    }
                }

                if (result.kind == ModelReference::Kind::None) {
                    // Other curves: use the picked point, which lies on the edge
                    Base::Matrix4D toSketchMatrix = viewProvider.getDocument()->getEditingTransform();
                    toSketchMatrix.inverseGauss();
                    const Base::Vector3d picked = toSketchMatrix * modelPick.point;

                    result.kind = ModelReference::Kind::Edge;
                    result.position = Base::Vector2d(picked.x, picked.y);
                }
            }
        }
        catch (const Base::Exception&) {
            result = ModelReference();
        }
        catch (const Standard_Failure&) {
            result = ModelReference();
        }
    }

    // The center of the circle hovered last, when the cursor gets close to it
    if (result.kind != ModelReference::Kind::Point && acquiredCenter.isValid()
        && (inputPos - acquiredCenter.position).Length()
            < viewProvider.getScaleFactor() * modelCenterSnapScale) {
        result = acquiredCenter;
    }

    if (!result.isValid()) {
        return false;
    }

    modelReference = result;
    snapPos = result.position;
    return true;
}

void SnapManager::clearModelPick()
{
    modelPick = ModelPick();
}

void SnapManager::updateModelPick(const SbVec2s& cursorPos, const Gui::View3DInventorViewer* viewer)
{
    clearModelPick();

    if (!viewer || !snapRequested || !snapToObjectsRequested || !snapToModelRequested) {
        return;
    }

    SoRayPickAction pickAction(viewer->getSoRenderManager()->getViewportRegion());
    pickAction.setPickAll(true);
    pickAction.setPoint(cursorPos);
    pickAction.setRadius(viewer->getPickRadius());
    pickAction.apply(viewer->getSoRenderManager()->getSceneGraph());

    // Only elements of the first object hit (the visible one) are considered. Among them a
    // vertex has precedence over an edge, faces are ignored.
    const App::DocumentObject* firstObject = nullptr;
    ModelPick edgePick;

    const SoPickedPointList& pickedPoints = pickAction.getPickedPointList();
    for (int i = 0; i < pickedPoints.getLength(); ++i) {
        const SoPickedPoint* pickedPoint = pickedPoints[i];
        if (!pickedPoint || !pickedPoint->getPath()) {
            continue;
        }

        Gui::ViewProvider* vp = viewer->getViewProviderByPath(pickedPoint->getPath());
        if (!vp || vp == &viewProvider || !vp->isDerivedFrom<Gui::ViewProviderDocumentObject>()) {
            continue;
        }

        std::string subName;
        if (!vp->getElementPicked(pickedPoint, subName) || subName.empty()) {
            continue;
        }

        App::DocumentObject* object = static_cast<Gui::ViewProviderDocumentObject*>(vp)->getObject();
        if (!object) {
            continue;
        }

        App::ElementNamePair elementName;
        App::DocumentObject* subObject
            = App::GeoFeature::resolveElement(object, subName.c_str(), elementName);
        if (!subObject || !subObject->isAttachedToDocument()) {
            continue;
        }

        if (!firstObject) {
            firstObject = subObject;
        }
        else if (subObject != firstObject) {
            break;
        }

        const std::string element = !elementName.oldName.empty() ? elementName.oldName
                                                                  : elementName.newName;
        const SbVec3f& point = pickedPoint->getPoint();
        ModelPick pick {
            subObject->getNameInDocument(),
            element,
            Base::Vector3d(point[0], point[1], point[2])
        };

        if (isVertexName(element)) {
            modelPick = std::move(pick);
            return;
        }
        if (isEdgeName(element) && edgePick.element.empty()) {
            edgePick = std::move(pick);
        }
    }

    modelPick = std::move(edgePick);
}

bool SnapManager::snapToGrid(Base::Vector2d inputPos, Base::Vector2d& snapPos)
{
    // Snap Tolerance in pixels
    const double snapTol = viewProvider.getGridSize() / 5;

    snapPos = inputPos;

    double tmpX = inputPos.x, tmpY = inputPos.y;

    viewProvider.getClosestGridPoint(tmpX, tmpY);

    bool snapped = false;

    // Check if x within snap tolerance
    if (inputPos.x < tmpX + snapTol && inputPos.x > tmpX - snapTol) {
        snapPos.x = tmpX;  // Snap X Mouse Position
        snapped = true;
    }

    // Check if y within snap tolerance
    if (inputPos.y < tmpY + snapTol && inputPos.y > tmpY - snapTol) {
        snapPos.y = tmpY;  // Snap Y Mouse Position
        snapped = true;
    }

    return snapped;
}

bool SnapManager::snapToLineMiddle(Base::Vector3d& pointToOverride, const Part::GeomLineSegment* line)
{
    Base::Vector3d startPoint = line->getStartPoint();
    Base::Vector3d endPoint = line->getEndPoint();
    Base::Vector3d midPoint = (startPoint + endPoint) / 2;

    // Check if we are at middle of the line and if so snap to it.
    if ((pointToOverride - midPoint).Length() < (endPoint - startPoint).Length() * 0.05) {
        pointToOverride = midPoint;
        return true;
    }

    return false;
}

bool SnapManager::snapToArcMiddle(Base::Vector3d& pointToOverride, const Part::GeomArcOfCircle* arc)
{
    Base::Vector3d centerPoint = arc->getCenter();
    Base::Vector3d startVec = (arc->getStartPoint() - centerPoint);
    Base::Vector3d middleVec = startVec + (arc->getEndPoint() - centerPoint);

    /* Handle the case of arc angle = 180 */
    if (middleVec.Length() < Precision::Confusion()) {
        middleVec.x = startVec.y;
        middleVec.y = -startVec.x;
    }
    else {
        middleVec = middleVec / middleVec.Length() * arc->getRadius();
    }

    Base::Vector2d mVec = Base::Vector2d(middleVec.x, middleVec.y);
    Base::Vector3d pointVec = pointToOverride - centerPoint;
    Base::Vector2d pVec = Base::Vector2d(pointVec.x, pointVec.y);

    double u, v;
    arc->getRange(u, v, true);
    if (v < u) {
        v += 2 * std::numbers::pi;
    }
    double angle = v - u;
    int revert = angle < std::numbers::pi ? 1 : -1;

    /*To know if we are close to the middle of the arc, we are going to compare the angle of the
     * (mouse cursor - center) to the angle of the middle of the arc. If it's less than 10% of the
     * arc angle, then we snap.
     */
    if (fabs(pVec.Angle() - (revert * mVec).Angle()) < 0.10 * angle) {
        pointToOverride = centerPoint + middleVec * revert;
        return true;
    }

    return false;
}

void SnapManager::setAngleSnapping(bool enable, Base::Vector2d referencepoint)
{
    angleSnapRequested = enable;
    referencePoint = referencepoint;
}

Base::Vector2d SketcherGui::SnapManager::SnapHandle::compute(SnapType mask)
{
    if (!mgr) {
        return cursorPos;
    }
    return mgr->snap(cursorPos, mask);
}

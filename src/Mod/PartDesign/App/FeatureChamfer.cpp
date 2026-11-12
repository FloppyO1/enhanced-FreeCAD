// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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
#include <limits>
#include <numbers>

#include <BRepAlgo.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <Standard_Version.hxx>


#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <Base/Tools.h>
#include <Mod/Part/App/SignalException.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/TopoShapeOpCode.h>

#include "FeatureChamfer.h"
#include "FeatureFillet.h"

#include <Base/ProgramVersion.h>


using namespace PartDesign;


PROPERTY_SOURCE(PartDesign::Chamfer, PartDesign::DressUp)

const char* ChamferTypeEnums[] = {"Equal distance", "Two distances", "Distance and Angle", nullptr};
const App::PropertyQuantityConstraint::Constraints Chamfer::floatSize
    = {0.0, std::numeric_limits<float>::max(), 0.1};
const App::PropertyAngle::Constraints Chamfer::floatAngle = {0.0, 180.0, 1.0};

static App::DocumentObjectExecReturn* validateParameters(
    int chamferType,
    double size,
    double size2,
    double angle
);

// Triangular prism of the chamfer, for the boolean fallback (see makeDressUpByBoolean)
static TopoDS_Shape makeChamferTool(
    const DressUpCorner& corner,
    int chamferType,
    double size,
    double size2,
    bool flipDirection,
    const std::string& featureName
)
{
    // the sizes are measured on the reference face: the first one, the last one if flipped
    const double angle = corner.u1.Angle(corner.u2);
    double distRef = size;
    double distOther = size;
    switch (static_cast<Part::ChamferType>(chamferType)) {
        case Part::ChamferType::equalDistance:
            break;
        case Part::ChamferType::twoDistances:
            distOther = size2;
            break;
        case Part::ChamferType::distanceAngle: {
            // size2 holds the angle between the chamfer and the reference face
            const double alpha = Base::toRadians(size2);
            const double opposite = std::numbers::pi - angle - alpha;
            if (opposite < Precision::Angular()) {
                return {};
            }
            distOther = size * std::sin(alpha) / std::sin(opposite);
            break;
        }
    }
    double d1 = flipDirection ? distOther : distRef;
    double d2 = flipDirection ? distRef : distOther;
    // a face bounded by a curved face that the tool would pass: scale the chamfer down
    const double factor = corner.sizeFactor(d1, d2);
    if (factor < 1.0) {
        d1 *= factor;
        d2 *= factor;
        Base::Console().warning(
            "{}: size of Edge{} reduced to {:.4g} so that the chamfer does not pass a curved "
            "face\n",
            featureName,
            corner.edgeIndex,
            size * factor
        );
    }

    const gp_Vec bisector = gp_Vec(gp_Dir(gp_Vec(corner.u1) + gp_Vec(corner.u2)));
    const gp_Vec dir(corner.dir);
    const double ext = corner.convex ? 0.1 * std::max(d1, d2) : 0.0;
    const gp_Pnt p = corner.start.Translated(-dir * ext);
    // move the corner out of the wedge, so that the tool overlaps the faces
    const gp_Pnt outer = p.Translated(-bisector * (0.1 * std::min(d1, d2)));
    const gp_Pnt a1 = p.Translated(gp_Vec(corner.u1) * d1);
    const gp_Pnt a2 = p.Translated(gp_Vec(corner.u2) * d2);

    BRepBuilderAPI_MakePolygon polygon(outer, a1, a2, Standard_True);
    BRepBuilderAPI_MakeFace face(polygon.Wire(), Standard_True);
    BRepPrimAPI_MakePrism prism(face.Face(), dir * (corner.length + 2 * ext));
    return prism.Shape();
}

Chamfer::Chamfer()
{
    ADD_PROPERTY_TYPE(ChamferType, (0L), "Chamfer", App::Prop_None, "Type of chamfer");
    ChamferType.setEnums(ChamferTypeEnums);

    ADD_PROPERTY_TYPE(Size, (1.0), "Chamfer", App::Prop_None, "Size of chamfer");
    Size.setUnit(Base::Unit::Length);
    Size.setConstraints(&floatSize);

    ADD_PROPERTY_TYPE(Size2, (1.0), "Chamfer", App::Prop_None, "Second size of chamfer");
    Size2.setUnit(Base::Unit::Length);
    Size2.setConstraints(&floatSize);

    ADD_PROPERTY_TYPE(Angle, (45.0), "Chamfer", App::Prop_None, "Angle of chamfer");
    Angle.setUnit(Base::Unit::Angle);
    Angle.setConstraints(&floatAngle);

    ADD_PROPERTY_TYPE(FlipDirection, (false), "Chamfer", App::Prop_None, "Flip direction");
    ADD_PROPERTY_TYPE(
        UseAllEdges,
        (false),
        "Chamfer",
        App::Prop_None,
        "Chamfer all edges if true, else use only those edges in Base property.\n"
        "If true, then this overrides any edge changes made to the Base property or in the "
        "dialog.\n"
    );
    ADD_PROPERTY_TYPE(
        SizeMode,
        (0L),
        "Chamfer",
        App::Prop_None,
        "Dimension: use Size.\n"
        "UpTo: compute Size so that the chamfer reaches the nearest of UpToReferences."
    );
    SizeMode.setEnums(DressUpSizeModeEnums);
    ADD_PROPERTY_TYPE(
        UpToReferences,
        (nullptr),
        "Chamfer",
        App::Prop_None,
        "Faces, edges or vertices that the chamfer reaches in UpTo mode"
    );

    updateProperties();
}

short Chamfer::mustExecute() const
{
    bool touched = false;

    auto chamferType = ChamferType.getValue();

    switch (chamferType) {
        case 0:  // "Equal distance"
            touched = Size.isTouched() || ChamferType.isTouched();
            break;
        case 1:  // "Two distances"
            touched = Size.isTouched() || ChamferType.isTouched() || Size2.isTouched();
            break;
        case 2:  // "Distance and Angle"
            touched = Size.isTouched() || ChamferType.isTouched() || Angle.isTouched();
            break;
    }

    touched = touched || SizeMode.isTouched() || UpToReferences.isTouched();

    if (Placement.isTouched() || touched) {
        return 1;
    }
    return DressUp::mustExecute();
}

App::DocumentObjectExecReturn* Chamfer::execute()
{
    if (onlyHaveRefined()) {
        return App::DocumentObject::StdReturn;
    }

    // NOTE: Normally the Base property and the BaseFeature property should point to the same object.
    // The only difference is that the Base property also stores the edges that are to be chamfered
    Part::TopoShape TopShape;
    try {
        TopShape = getBaseTopoShape();
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    const TopLoc_Location baseLocation = TopShape.getShape().Location();
    TopShape.setTransform(Base::Matrix4D());

    auto edges = UseAllEdges.getValue() ? TopShape.getSubTopoShapes(TopAbs_EDGE)
                                        : getContinuousEdges(TopShape);

    if (edges.empty()) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "No edges specified"));
    }
    const int chamferType = ChamferType.getValue();
    double size = Size.getValue();
    if (SizeMode.getValue() == 1) {
        try {
            // the edge of the chamfer on the face lies on the reference
            size = computeUpToSize(
                TopShape,
                edges,
                getUpToReferenceShapes(UpToReferences, baseLocation),
                [](const DressUpCorner&, double distance) { return distance; }
            );
        }
        catch (Base::Exception& e) {
            return new App::DocumentObjectExecReturn(e.what());
        }
        // show the computed size (purged with the other touched properties by the recompute)
        if (std::abs(Size.getValue() - size) > Precision::Confusion()) {
            Size.setValue(size);
        }
    }
    double size2 = Size2.getValue();
    const double angle = Angle.getValue();
    const bool flipDirection = FlipDirection.getValue();

    auto res = validateParameters(chamferType, size, size2, angle);
    if (res != App::DocumentObject::StdReturn) {
        return res;
    }

    this->positionByBaseFeature();

    if (static_cast<Part::ChamferType>(chamferType) == Part::ChamferType::distanceAngle) {
        size2 = angle;
    }
    try {
        TopoShape shape(0);
        Part::SignalException sig;
        usedBooleanFallback = false;
        std::string occError;
        try {
            shape.makeElementChamfer(
                TopShape,
                edges,
                static_cast<Part::ChamferType>(chamferType),
                size,
                size2,
                nullptr,
                flipDirection ? Part::Flip::flip : Part::Flip::none
            );
        }
        catch (Standard_Failure& e) {
            occError = e.GetMessageString();
            shape = TopoShape(0);
        }
        catch (Base::Exception& e) {
            occError = e.what();
            shape = TopoShape(0);
        }

        // OCC fails when the chamfer consumes a whole face: try the boolean fallback
        if (!isDressUpResultValid(TopShape, shape)) {
            auto makeTool = [&](const DressUpCorner& corner) {
                return makeChamferTool(
                    corner,
                    chamferType,
                    size,
                    size2,
                    flipDirection,
                    getFullName()
                );
            };
            TopoShape fallback
                = makeDressUpByBoolean(TopShape, edges, makeTool, Part::OpCodes::Chamfer);
            if (!fallback.isNull()) {
                Base::Console().log("{}: chamfer made with the boolean fallback\n", getFullName());
                shape = fallback;
                usedBooleanFallback = true;
            }
            else if (!occError.empty()) {
                return new App::DocumentObjectExecReturn(occError);
            }
        }
        if (shape.isNull()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Failed to create chamfer")
            );
        }

        TopTools_ListOfShape aLarg;
        aLarg.Append(TopShape.getShape());
        if (!BRepAlgo::IsValid(aLarg, shape.getShape(), Standard_False, Standard_False)) {
            ShapeFix_ShapeTolerance aSFT;
            aSFT.LimitTolerance(
                shape.getShape(),
                Precision::Confusion(),
                Precision::Confusion(),
                TopAbs_SHAPE
            );
        }

        // store shape before refinement
        this->rawShape = shape;
        shape = refineShapeIfActive(shape);
        if (!isSingleSolidRuleSatisfied(shape.getShape())) {
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
                "Exception",
                "Result has multiple solids: enable 'Allow Compound' in the active body."
            ));
        }

        shape = getSolid(shape);
        this->Shape.setValue(shape);
        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
    catch (...) {
        return new App::DocumentObjectExecReturn(
            QT_TRANSLATE_NOOP("Exception", "Chamfer failed: OCC kernel error in chamfer computation")
        );
    }
}

void Chamfer::Restore(Base::XMLReader& reader)
{
    DressUp::Restore(reader);

    migrateFlippedProperties(reader);
}

void Chamfer::handleChangedPropertyType(Base::XMLReader& reader, const char* TypeName, App::Property* prop)
{
    if (prop && strcmp(TypeName, "App::PropertyFloatConstraint") == 0
        && prop->getTypeId().getName() == "App::PropertyQuantityConstraint") {
        App::PropertyFloatConstraint p;
        p.Restore(reader);
        static_cast<App::PropertyQuantityConstraint*>(prop)->setValue(p.getValue());
    }
    else {
        DressUp::handleChangedPropertyType(reader, TypeName, prop);
    }
}

bool Chamfer::requiresSizeSwapping(const Base::XMLReader& reader) const
{
    return Base::getVersion(reader.ProgramVersion) < Base::Version::v1_0
        && (ChamferType.getValue() == 1 || ChamferType.getValue() == 2);
}

void Chamfer::migrateFlippedProperties(const Base::XMLReader& reader)
{
    if (!requiresSizeSwapping(reader)) {
        return;
    }

    Base::Console().warning(
        "The 'FlipDirection' property of the chamfer of {} is being adjusted to maintain"
        "the same geometry in this FreeCAD version. If the re-saved file is later opened "
        "in FreeCAD 0.21.x the chamfer result may differ due to the changed parameter "
        "interpretation.\n",
        getFullName()
    );

    FlipDirection.setValue(!FlipDirection.getValue());
}

void Chamfer::onChanged(const App::Property* prop)
{
    if (prop == &ChamferType || prop == &SizeMode) {
        updateProperties();
    }

    DressUp::onChanged(prop);
}

void Chamfer::updateProperties()
{
    auto chamferType = ChamferType.getValue();

    auto disableproperty = [](App::Property* prop, bool on) {
        prop->setStatus(App::Property::ReadOnly, on);
    };

    switch (chamferType) {
        case 0:  // "Equal distance"
            disableproperty(&this->Angle, true);
            disableproperty(&this->Size2, true);
            break;
        case 1:  // "Two distances"
            disableproperty(&this->Angle, true);
            disableproperty(&this->Size2, false);
            break;
        case 2:  // "Distance and Angle"
            disableproperty(&this->Angle, false);
            disableproperty(&this->Size2, true);
            break;
    }
    // in UpTo mode the size is computed
    disableproperty(&this->Size, SizeMode.getValue() == 1);
}

static App::DocumentObjectExecReturn* validateParameters(
    int chamferType,
    double size,
    double size2,
    double angle
)
{
    // Size is common to all chamfer types.
    if (size <= 0) {
        return new App::DocumentObjectExecReturn(
            QT_TRANSLATE_NOOP("Exception", "Size must be greater than zero")
        );
    }

    switch (chamferType) {
        case 0:  // Equal distance
            // Nothing to do.
            break;
        case 1:  // Two distances
            if (size2 <= 0) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Size2 must be greater than zero")
                );
            }
            break;
        case 2:  // Distance and angle
            if (angle <= 0 || angle >= 180.0) {
                return new App::DocumentObjectExecReturn(
                    QT_TRANSLATE_NOOP("Exception", "Angle must be greater than 0 and less than 180")
                );
            }
            break;
    }

    return App::DocumentObject::StdReturn;
}

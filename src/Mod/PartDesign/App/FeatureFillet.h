// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2008 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#include <functional>
#include <limits>

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include "FeatureDressUp.h"

namespace PartDesign
{

/// Straight edge between two planar faces, as seen by the boolean fallback of fillet/chamfer
struct DressUpCorner
{
    gp_Pnt start;     ///< first point of the edge
    gp_Dir dir;       ///< direction of the edge
    double length;    ///< length of the edge
    gp_Dir u1, u2;    ///< directions inside the first/second face, normal to the edge
    bool convex;      ///< true: material between u1 and u2 (cut); false: empty (fuse)
    /// largest distance along u1/u2 that the tool may cover on the first/second face, set when
    /// that face is bounded by a curved face that clipping cannot handle (infinite otherwise)
    double limit1 = std::numeric_limits<double>::infinity();
    double limit2 = std::numeric_limits<double>::infinity();
    int edgeIndex = 0;  ///< index of the edge in the base shape (1-based, 0 if unknown)

    /** Factor (<= 1) by which to scale a tool covering @a d1 along u1 and @a d2 along u2, so
     * that it stays inside limit1/limit2 with a small safety margin */
    double sizeFactor(double d1, double d2) const;
};

/** Fallback for fillet/chamfer when OCC fails: the tool built by @a makeTool for each edge
 * (a prism in the wedge between u1 and u2) is cut from the solid for convex edges and fused
 * for concave edges. Each tool is first clipped to the solid's envelope by the planes of the
 * planar faces next to the two faces of the edge (see clipPlanes), so that a fused tool never
 * sticks out of the solid and a cut tool never digs into a floor at the end of the edge. Returns a null shape if an edge is not supported (not a straight edge
 * between two planar faces) or if the result is not valid.
 */
Part::TopoShape makeDressUpByBoolean(
    const Part::TopoShape& base,
    const std::vector<Part::TopoShape>& edges,
    const std::function<TopoDS_Shape(const DressUpCorner&)>& makeTool,
    const char* op
);

/// True if @a shape is valid (BRepCheck) and has as many solids as @a base
bool isDressUpResultValid(const Part::TopoShape& base, const Part::TopoShape& shape);

/** Shapes of the "Up to" references of a fillet/chamfer, in the frame of the base shape whose
 * placement is @a baseLocation (the base shape is used with an identity transform)
 */
std::vector<Part::TopoShape> getUpToReferenceShapes(
    const App::PropertyLinkSubList& references,
    const TopLoc_Location& baseLocation
);

/** Size of a fillet/chamfer in "Up to" mode: for each edge (straight, between two planar faces)
 * and each of its two faces, the distance from the edge to the nearest reference, measured in the
 * face perpendicular to the edge (references that give no positive distance are ignored). The
 * smallest distance d of an edge is converted by @a toSize(corner, d) and the smallest size over
 * all edges is returned. Throws Base::ValueError if no reference gives a distance.
 */
double computeUpToSize(
    const Part::TopoShape& base,
    const std::vector<Part::TopoShape>& edges,
    const std::vector<Part::TopoShape>& references,
    const std::function<double(const DressUpCorner&, double)>& toSize
);

/// Values of the SizeMode property of fillet/chamfer
extern const char* DressUpSizeModeEnums[];

class PartDesignExport Fillet: public DressUp
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::Fillet);

public:
    Fillet();

    App::PropertyQuantityConstraint Radius;
    App::PropertyBool UseAllEdges;
    App::PropertyEnumeration SizeMode;
    App::PropertyLinkSubList UpToReferences;

    /** @name methods override feature */
    //@{
    /// recalculate the feature
    App::DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    /// returns the type name of the view provider
    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderFillet";
    }
    //@}

    void onChanged(const App::Property* prop) override;

protected:
    void Restore(Base::XMLReader& reader) override;
    void handleChangedPropertyType(
        Base::XMLReader& reader,
        const char* TypeName,
        App::Property* prop
    ) override;
};

}  // namespace PartDesign

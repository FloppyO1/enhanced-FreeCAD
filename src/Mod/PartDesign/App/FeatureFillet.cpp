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

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgo.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <gp_Circ.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Trsf.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Circle.hxx>
#include <Geom2d_Curve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_ShapeTolerance.hxx>

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <App/DocumentObject.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/TopoShapeOpCode.h>

#include "FeatureFillet.h"


using namespace PartDesign;

namespace
{

// Outward normal of @a face at the parameter @a t of its boundary @a edge
bool faceNormalAt(const TopoDS_Face& face, const TopoDS_Edge& edge, double t, gp_Dir& normal)
{
    double first, last;
    Handle(Geom2d_Curve) pcurve = BRep_Tool::CurveOnSurface(edge, face, first, last);
    if (pcurve.IsNull()) {
        return false;
    }
    const gp_Pnt2d uv = pcurve->Value(t);
    BRepAdaptor_Surface surface(face);
    gp_Pnt point;
    gp_Vec du, dv;
    surface.D1(uv.X(), uv.Y(), point, du, dv);
    const gp_Vec n = du.Crossed(dv);
    if (n.Magnitude() < Precision::Confusion()) {
        return false;
    }
    normal = gp_Dir(n);
    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    return true;
}

/* Checks that the circle of @a curve is the junction of the planar face and the cylindrical
 * face in @a faces (cylinder axis = circle axis, same radius) and fills the circular fields of
 * @a corner
 */
bool analyseCircle(
    const BRepAdaptor_Curve& curve,
    const std::vector<TopoDS_Shape>& faces,
    DressUpCorner& corner
)
{
    constexpr double angularTol = 1e-9;
    const double tol = Precision::Confusion();
    const gp_Circ circ = curve.Circle();
    const gp_Dir axis = circ.Axis().Direction();
    int planes = 0;
    int cylinders = 0;
    for (const auto& faceShape : faces) {
        BRepAdaptor_Surface surface(TopoDS::Face(faceShape));
        if (surface.GetType() == GeomAbs_Plane) {
            if (std::abs(surface.Plane().Axis().Direction().Dot(axis)) < 1 - angularTol) {
                return false;
            }
            ++planes;
        }
        else if (surface.GetType() == GeomAbs_Cylinder) {
            const gp_Cylinder cylinder = surface.Cylinder();
            if (std::abs(cylinder.Axis().Direction().Dot(axis)) < 1 - angularTol
                || gp_Lin(cylinder.Axis()).Distance(circ.Location()) > tol
                || std::abs(cylinder.Radius() - circ.Radius()) > tol) {
                return false;
            }
            ++cylinders;
        }
    }
    if (planes != 1 || cylinders != 1) {
        return false;
    }
    corner.circular = true;
    corner.radius = circ.Radius();
    corner.sweep = curve.LastParameter() - curve.FirstParameter();
    if (corner.radius < tol || corner.sweep < Precision::Angular()
        || corner.sweep > 2 * std::numbers::pi + Precision::Angular()) {
        return false;
    }
    // the parameter of a circle turns counterclockwise around its axis
    corner.circle = gp_Ax2(circ.Location(), axis, gp_Vec(circ.Location(), corner.start));
    corner.length = corner.radius * corner.sweep;
    return true;
}

/* Fills @a corner for a straight edge between two planar faces of @a base, or a circular edge
 * between a planar face and a cylindrical face around the axis of the circle
 */
bool analyseCorner(const TopoShape& base, const TopoShape& edge, DressUpCorner& corner)
{
    if (edge.isNull() || edge.getShape().ShapeType() != TopAbs_EDGE) {
        return false;
    }
    const TopoDS_Edge& e = TopoDS::Edge(edge.getShape());
    BRepAdaptor_Curve curve(e);
    if (curve.GetType() != GeomAbs_Line && curve.GetType() != GeomAbs_Circle) {
        return false;
    }
    auto faces = base.findAncestorsShapes(e, TopAbs_FACE);
    if (faces.size() != 2) {
        return false;
    }
    const double first = curve.FirstParameter();
    gp_Vec startTangent;
    curve.D1(first, corner.start, startTangent);
    if (startTangent.Magnitude() < Precision::Confusion()) {
        return false;
    }
    const gp_Dir lineDir(startTangent);
    corner.dir = lineDir;
    if (curve.GetType() == GeomAbs_Line) {
        corner.length = curve.LastParameter() - first;
        if (corner.length < Precision::Confusion()) {
            return false;
        }
    }
    else {
        if (!analyseCircle(curve, faces, corner)) {
            return false;
        }
    }

    gp_Dir normals[2];
    gp_Dir inFace[2];
    for (int i = 0; i < 2; ++i) {
        const TopoDS_Face& face = TopoDS::Face(faces[i]);
        gp_Dir normal;
        if (corner.circular) {
            // normal at the start, in the section through the axis
            if (!faceNormalAt(face, e, first, normal)) {
                return false;
            }
        }
        else {
            BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Plane) {
                return false;
            }
            normal = surface.Plane().Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) {
                normal.Reverse();
            }
        }
        // orientation of the edge in the face (already composed with the face orientation)
        int found = 0;
        TopAbs_Orientation orientation = TopAbs_FORWARD;
        for (TopExp_Explorer xp(face, TopAbs_EDGE); xp.More(); xp.Next()) {
            if (xp.Current().IsSame(e)) {
                orientation = xp.Current().Orientation();
                ++found;
            }
        }
        if (found != 1
            || (orientation != TopAbs_FORWARD && orientation != TopAbs_REVERSED)) {
            return false;
        }
        const gp_Dir tangent = orientation == TopAbs_FORWARD ? lineDir : lineDir.Reversed();
        normals[i] = normal;
        // the face material is on the left of the oriented edge, seen from the normal
        inFace[i] = normal.Crossed(tangent);
    }
    corner.u1 = inFace[0];
    corner.u2 = inFace[1];
    const double angle = corner.u1.Angle(corner.u2);
    if (angle < Precision::Angular() * 100 || angle > std::numbers::pi - Precision::Angular() * 100) {
        return false;  // tangent faces
    }
    const double side = gp_Vec(normals[0]).Dot(gp_Vec(corner.u2));
    corner.convex = side < 0;
    return true;
}

// Outward normal of a planar face
bool planeOf(const TopoDS_Face& face, gp_Pnt& origin, gp_Dir& normal)
{
    BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        return false;
    }
    origin = surface.Plane().Location();
    normal = surface.Plane().Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED) {
        normal.Reverse();
    }
    return true;
}

// Direction inside @a face, normal to its boundary @a edge at the middle point of the edge
bool inFaceDirection(const TopoDS_Face& face, const TopoDS_Edge& edge, gp_Pnt& mid, gp_Dir& dir)
{
    TopAbs_Orientation orientation = TopAbs_INTERNAL;
    for (TopExp_Explorer xp(face, TopAbs_EDGE); xp.More(); xp.Next()) {
        if (xp.Current().IsSame(edge)) {
            if (orientation != TopAbs_INTERNAL) {
                return false;  // seam or edge used twice
            }
            orientation = xp.Current().Orientation();
        }
    }
    if (orientation != TopAbs_FORWARD && orientation != TopAbs_REVERSED) {
        return false;
    }
    BRepAdaptor_Curve curve(edge);
    const double t = (curve.FirstParameter() + curve.LastParameter()) / 2;
    gp_Vec tangent;
    curve.D1(t, mid, tangent);
    gp_Dir normal;
    if (tangent.Magnitude() < Precision::Confusion() || !faceNormalAt(face, edge, t, normal)) {
        return false;
    }
    if (orientation == TopAbs_REVERSED) {
        tangent.Reverse();
    }
    // the face material is on the left of the oriented edge, seen from the normal
    dir = normal.Crossed(gp_Dir(tangent));
    return true;
}

// Half-space removed from a tool: the side of the plane where the normal points
struct ClipPlane
{
    gp_Pnt origin;
    gp_Dir normal;
};

/* Planes that keep the tool of @a edge inside the solid's envelope. For each of the two faces
 * of the edge, look at the planar faces next to it along its other edges:
 * - concave corner (the tool is fused): a convex junction is a boundary of the solid, the tool
 *   must stay on its material side;
 * - convex corner (the tool is cut): a concave junction is material that does not belong to the
 *   corner (e.g. the floor at the foot of a boss), the tool must stay on its air side.
 * Other junctions need no clipping: there the tool only overlaps material that is kept (fuse)
 * or air (cut).
 */
std::vector<ClipPlane> clipPlanes(const TopoShape& base, const TopoDS_Edge& edge, bool convex)
{
    std::vector<ClipPlane> planes;
    for (const auto& faceShape : base.findAncestorsShapes(edge, TopAbs_FACE)) {
        const TopoDS_Face& face = TopoDS::Face(faceShape);
        for (TopExp_Explorer xp(face, TopAbs_EDGE); xp.More(); xp.Next()) {
            const TopoDS_Edge& other = TopoDS::Edge(xp.Current());
            if (other.IsSame(edge) || BRep_Tool::Degenerated(other)) {
                continue;
            }
            for (const auto& nextShape : base.findAncestorsShapes(other, TopAbs_FACE)) {
                if (nextShape.IsSame(face)) {
                    continue;
                }
                const TopoDS_Face& next = TopoDS::Face(nextShape);
                ClipPlane plane;
                gp_Pnt mid;
                gp_Dir inFace;
                if (!planeOf(next, plane.origin, plane.normal)
                    || !inFaceDirection(face, other, mid, inFace)) {
                    continue;
                }
                const double side = gp_Vec(plane.normal).Dot(gp_Vec(inFace));
                if (std::abs(side) < 1e-6) {
                    continue;  // tangent junction
                }
                const bool junctionConvex = side < 0;
                if (!convex && junctionConvex) {
                    planes.push_back(plane);  // remove the air side
                }
                else if (convex && !junctionConvex) {
                    plane.normal.Reverse();  // remove the material side
                    planes.push_back(plane);
                }
            }
        }
    }
    return planes;
}

// Removes from @a tool the half-spaces of @a planes
TopoDS_Shape clipTool(const TopoDS_Shape& tool, const std::vector<ClipPlane>& planes)
{
    TopoDS_Shape result = tool;
    for (const auto& plane : planes) {
        Bnd_Box box;
        BRepBndLib::Add(result, box);
        if (box.IsVoid()) {
            return {};
        }
        const gp_Pln pln(plane.origin, plane.normal);
        // skip the planes that do not cross the tool
        double xmin, ymin, zmin, xmax, ymax, zmax;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        double dmax = -std::numeric_limits<double>::max();
        for (int i = 0; i < 8; ++i) {
            const gp_Pnt p(i & 1 ? xmax : xmin, i & 2 ? ymax : ymin, i & 4 ? zmax : zmin);
            const double d = gp_Vec(plane.origin, p).Dot(gp_Vec(plane.normal));
            dmax = std::max(dmax, d);
        }
        if (dmax <= Precision::Confusion()) {
            continue;  // the tool is entirely on the kept side
        }
        const double size = std::sqrt(box.SquareExtent()) + 1.0;
        // project the box center on the plane, so that the cutting block covers the tool
        const gp_Pnt boxCenter((xmin + xmax) / 2, (ymin + ymax) / 2, (zmin + zmax) / 2);
        const gp_Pnt center = boxCenter.Translated(
            -gp_Vec(plane.normal) * gp_Vec(plane.origin, boxCenter).Dot(gp_Vec(plane.normal))
        );
        const gp_Ax2 axes(center, plane.normal);
        const gp_Pnt corner = center.Translated(
            -gp_Vec(axes.XDirection()) * size - gp_Vec(axes.YDirection()) * size
        );
        BRepPrimAPI_MakeBox block(
            gp_Ax2(corner, plane.normal, axes.XDirection()),
            2 * size,
            2 * size,
            dmax + size
        );
        BRepAlgoAPI_Cut cut(result, block.Shape());
        if (!cut.IsDone()) {
            return {};
        }
        result = cut.Shape();
        if (TopExp_Explorer(result, TopAbs_SOLID).More() == Standard_False) {
            return result;  // nothing left
        }
    }
    return result;
}

/* Limits of the tool on the two faces of the edge that clipping cannot provide. clipPlanes
 * only uses planar neighbours; where a face of the edge is bounded by a curved face at a
 * junction that would need clipping (convex for a fused tool, concave for a cut tool), the
 * tool must not reach that boundary. The limit is the smallest distance from the edge,
 * measured in the face along u1/u2, of the points of those boundaries that lie beside the
 * edge (strictly between its ends). At a tangent junction the side the curved face bends
 * toward decides.
 */
void curvedBoundaryLimits(const TopoShape& base, const TopoDS_Edge& edge, DressUpCorner& corner)
{
    constexpr int samples = 64;
    const double tol = Precision::Confusion();
    auto faces = base.findAncestorsShapes(edge, TopAbs_FACE);
    if (faces.size() != 2) {
        return;
    }
    for (int i = 0; i < 2; ++i) {
        const TopoDS_Face& face = TopoDS::Face(faces[i]);
        double& limit = i == 0 ? corner.limit1 : corner.limit2;
        for (TopExp_Explorer xp(face, TopAbs_EDGE); xp.More(); xp.Next()) {
            const TopoDS_Edge& other = TopoDS::Edge(xp.Current());
            if (other.IsSame(edge) || BRep_Tool::Degenerated(other)
                || (other.Orientation() != TopAbs_FORWARD
                    && other.Orientation() != TopAbs_REVERSED)) {
                continue;
            }
            for (const auto& nextShape : base.findAncestorsShapes(other, TopAbs_FACE)) {
                if (nextShape.IsSame(face)) {
                    continue;
                }
                const TopoDS_Face& next = TopoDS::Face(nextShape);
                BRepAdaptor_Surface surface(next);
                if (surface.GetType() == GeomAbs_Plane) {
                    continue;  // handled by clipPlanes
                }
                double first, last;
                Handle(Geom2d_Curve) pcurve = BRep_Tool::CurveOnSurface(other, next, first, last);
                if (pcurve.IsNull()) {
                    continue;
                }
                BRepAdaptor_Curve curve(other);
                for (int k = 0; k <= samples; ++k) {
                    const double t = first + (last - first) * k / samples;
                    gp_Pnt p;
                    gp_Vec tangent;
                    curve.D1(t, p, tangent);
                    double along, d1, d2;
                    corner.locate(p, along, d1, d2);
                    const double dist = i == 0 ? d1 : d2;
                    gp_Dir faceNormal;
                    if (along <= tol || along >= corner.length - tol || dist <= tol
                        || dist >= limit || tangent.Magnitude() < tol
                        || !faceNormalAt(face, other, t, faceNormal)) {
                        continue;
                    }
                    // direction into the face from the boundary
                    if (other.Orientation() == TopAbs_REVERSED) {
                        tangent.Reverse();
                    }
                    const gp_Vec inFace = gp_Vec(faceNormal).Crossed(tangent).Normalized();
                    // side of the outward normal of the curved face at (u, v) along inFace,
                    // negative where the curved face turns away from the material
                    auto sideAt = [&](double su, double sv, double& side) {
                        gp_Pnt q;
                        gp_Vec du, dv;
                        surface.D1(su, sv, q, du, dv);
                        gp_Vec normal = du.Crossed(dv);
                        if (normal.Magnitude() < tol) {
                            return false;
                        }
                        normal.Normalize();
                        if (next.Orientation() == TopAbs_REVERSED) {
                            normal.Reverse();
                        }
                        side = normal.Dot(inFace);
                        return true;
                    };
                    const gp_Pnt2d uv = pcurve->Value(t);
                    double side;
                    if (!sideAt(uv.X(), uv.Y(), side)) {
                        continue;
                    }
                    if (std::abs(side) < 1e-6) {
                        // tangent junction: the side the curved face bends toward decides,
                        // from its normal a little way inside it, past the boundary
                        constexpr double step = 0.01;
                        GeomAPI_ProjectPointOnSurf projection(
                            p.Translated(-inFace * step),
                            BRep_Tool::Surface(next)
                        );
                        if (projection.NbPoints() < 1) {
                            continue;
                        }
                        double su, sv;
                        projection.LowerDistanceParameters(su, sv);
                        if (!sideAt(su, sv, side) || std::abs(side) < 1e-9) {
                            continue;  // flat in that direction
                        }
                    }
                    const bool junctionConvex = side < 0;
                    if (junctionConvex != corner.convex) {
                        limit = dist;
                    }
                }
            }
        }
    }
}

}  // namespace

double DressUpCorner::sizeFactor(double d1, double d2) const
{
    constexpr double margin = 0.99;
    double factor = 1.0;
    if (d1 > limit1 - Precision::Confusion()) {
        factor = std::min(factor, margin * limit1 / d1);
    }
    if (d2 > limit2 - Precision::Confusion()) {
        factor = std::min(factor, margin * limit2 / d2);
    }
    return factor;
}

void DressUpCorner::locate(const gp_Pnt& p, double& along, double& d1, double& d2) const
{
    if (!circular) {
        const gp_Vec rel(start, p);
        along = rel.Dot(gp_Vec(dir));
        d1 = rel.Dot(gp_Vec(u1));
        d2 = rel.Dot(gp_Vec(u2));
        return;
    }
    const gp_Vec v(circle.Location(), p);
    const gp_Vec axis(circle.Direction());
    const gp_Vec x(circle.XDirection());
    const double h = v.Dot(axis);
    const double px = v.Dot(x);
    const double py = v.Dot(gp_Vec(circle.YDirection()));
    double angle = std::atan2(py, px);
    if (angle < 0) {
        angle += 2 * std::numbers::pi;
    }
    along = angle * radius;
    // offset in the section at start, where the radial direction is x
    const gp_Vec rel = x * (std::hypot(px, py) - radius) + axis * h;
    d1 = rel.Dot(gp_Vec(u1));
    d2 = rel.Dot(gp_Vec(u2));
}

TopoDS_Shape PartDesign::sweepDressUpProfile(
    const DressUpCorner& corner,
    const TopoDS_Shape& profile,
    double ext
)
{
    if (profile.IsNull()) {
        return {};
    }
    if (!corner.circular) {
        gp_Trsf move;
        move.SetTranslation(-gp_Vec(corner.dir) * ext);
        BRepBuilderAPI_Transform moved(profile, move, Standard_True);
        BRepPrimAPI_MakePrism prism(moved.Shape(), gp_Vec(corner.dir) * (corner.length + 2 * ext));
        return prism.Shape();
    }

    // the profile must stay on the side of the axis where the edge is
    const gp_Ax1 axis(corner.circle.Location(), corner.circle.Direction());
    const gp_Vec radial(corner.circle.XDirection());
    for (TopExp_Explorer xp(profile, TopAbs_EDGE); xp.More(); xp.Next()) {
        BRepAdaptor_Curve curve(TopoDS::Edge(xp.Current()));
        constexpr int samples = 16;
        for (int k = 0; k <= samples; ++k) {
            const double t = curve.FirstParameter()
                + (curve.LastParameter() - curve.FirstParameter()) * k / samples;
            if (gp_Vec(corner.circle.Location(), curve.Value(t)).Dot(radial)
                < -Precision::Confusion()) {
                return {};
            }
        }
    }

    constexpr double fullTurn = 2 * std::numbers::pi;
    double extAngle = 0;
    if (corner.sweep < fullTurn - Precision::Angular()) {
        // arc length ext at the radius of the edge, without closing the turn
        extAngle = std::min(ext / corner.radius, (fullTurn - corner.sweep) / 4);
    }
    gp_Trsf rotation;
    rotation.SetRotation(axis, -extAngle);
    BRepBuilderAPI_Transform rotated(profile, rotation, Standard_True);
    BRepPrimAPI_MakeRevol revol(
        rotated.Shape(),
        axis,
        std::min(corner.sweep + 2 * extAngle, fullTurn)
    );
    if (!revol.IsDone()) {
        return {};
    }
    return revol.Shape();
}

bool PartDesign::isDressUpResultValid(const TopoShape& base, const TopoShape& shape)
{
    if (shape.isNull()) {
        return false;
    }
    if (shape.countSubShapes(TopAbs_SOLID) != base.countSubShapes(TopAbs_SOLID)) {
        return false;
    }
    BRepCheck_Analyzer analyzer(shape.getShape());
    return analyzer.IsValid();
}

const char* PartDesign::DressUpSizeModeEnums[] = {"Dimension", "UpTo", nullptr};

std::vector<TopoShape> PartDesign::getUpToReferenceShapes(
    const App::PropertyLinkSubList& references,
    const TopLoc_Location& baseLocation
)
{
    const auto options = Part::ShapeOption::NeedSubElement | Part::ShapeOption::ResolveLink
        | Part::ShapeOption::Transform;
    const TopLoc_Location inverse = baseLocation.Inverted();
    std::vector<TopoShape> shapes;
    for (const auto& [object, subs] : references.getSubListValues()) {
        if (!object) {
            continue;
        }
        std::vector<std::string> names = subs;
        if (names.empty()) {
            names.emplace_back();
        }
        for (const auto& name : names) {
            TopoShape shape = Part::Feature::getTopoShape(object, options, name.c_str());
            if (shape.isNull()) {
                throw Base::ValueError(
                    std::string("Invalid 'Up to' reference: ") + object->getFullName() + " " + name
                );
            }
            shape.move(inverse);
            shapes.push_back(shape);
        }
    }
    return shapes;
}

double PartDesign::computeUpToSize(
    const TopoShape& base,
    const std::vector<TopoShape>& edges,
    const std::vector<TopoShape>& references,
    const std::function<double(const DressUpCorner&, double)>& toSize
)
{
    if (references.empty()) {
        throw Base::ValueError("No reference selected for the 'Up to' mode");
    }
    const double tol = Precision::Confusion();
    Bnd_Box box;
    BRepBndLib::Add(base.getShape(), box);
    for (const auto& reference : references) {
        BRepBndLib::Add(reference.getShape(), box);
    }
    // half size of the planar faces used to measure the distances: they cover everything
    const double extent = std::sqrt(box.SquareExtent()) + 1.0;

    double size = std::numeric_limits<double>::infinity();
    bool supported = false;
    for (const auto& edge : edges) {
        DressUpCorner corner;
        if (!analyseCorner(base, edge, corner)) {
            continue;
        }
        supported = true;
        double nearest = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 2; ++i) {
            const gp_Dir& u = i == 0 ? corner.u1 : corner.u2;
            // surface through the edge, normal to the direction in the face: the distance of a
            // point from it is its distance from the edge, measured in the face along u. It is
            // a plane, except for the radial direction of a circular edge (a cylinder).
            TopoDS_Face surface;
            if (!corner.circular) {
                BRepBuilderAPI_MakeFace plane(
                    gp_Pln(gp_Ax3(corner.start, u)),
                    -extent,
                    extent,
                    -extent,
                    extent
                );
                if (plane.IsDone()) {
                    surface = plane.Face();
                }
            }
            else if (std::abs(gp_Vec(u).Dot(gp_Vec(corner.circle.Direction()))) > 0.5) {
                // along the axis, on the cylindrical face: plane of the circle
                BRepBuilderAPI_MakeFace plane(
                    gp_Pln(gp_Ax3(corner.circle.Location(), corner.circle.Direction())),
                    -extent,
                    extent,
                    -extent,
                    extent
                );
                if (plane.IsDone()) {
                    surface = plane.Face();
                }
            }
            else {
                // radial, on the planar face: cylinder through the circle
                BRepBuilderAPI_MakeFace cylinder(
                    gp_Cylinder(gp_Ax3(corner.circle), corner.radius),
                    0,
                    2 * std::numbers::pi,
                    -extent,
                    extent
                );
                if (cylinder.IsDone()) {
                    surface = cylinder.Face();
                }
            }
            if (surface.IsNull()) {
                continue;
            }
            for (const auto& reference : references) {
                BRepExtrema_DistShapeShape extrema(surface, reference.getShape());
                if (!extrema.IsDone() || extrema.NbSolution() < 1) {
                    continue;
                }
                const double distance = extrema.Value();
                if (distance <= tol) {
                    continue;  // the reference crosses or touches the plane
                }
                double along, d1, d2;
                corner.locate(extrema.PointOnShape2(1), along, d1, d2);
                if ((i == 0 ? d1 : d2) <= 0) {
                    continue;  // behind the edge, outside the face
                }
                nearest = std::min(nearest, distance);
            }
        }
        if (std::isfinite(nearest)) {
            size = std::min(size, toSize(corner, nearest));
        }
    }
    if (!supported) {
        throw Base::ValueError(
            "The 'Up to' mode needs straight edges between two planar faces, or circular edges "
            "between a planar face and a cylindrical face"
        );
    }
    if (!std::isfinite(size) || size <= tol) {
        throw Base::ValueError("No 'Up to' reference lies beside the edges, in their adjacent faces");
    }
    return size;
}

TopoShape PartDesign::makeDressUpByBoolean(
    const TopoShape& base,
    const std::vector<TopoShape>& edges,
    const std::function<TopoDS_Shape(const DressUpCorner&)>& makeTool,
    const char* op
)
{
    std::vector<TopoShape> cutShapes {base};
    std::vector<TopoShape> fuseShapes;
    for (const auto& edge : edges) {
        DressUpCorner corner;
        if (!analyseCorner(base, edge, corner)) {
            return {};
        }
        curvedBoundaryLimits(base, TopoDS::Edge(edge.getShape()), corner);
        corner.edgeIndex = base.findShape(edge.getShape());
        TopoDS_Shape tool = makeTool(corner);
        if (tool.IsNull()) {
            return {};
        }
        tool = clipTool(tool, clipPlanes(base, TopoDS::Edge(edge.getShape()), corner.convex));
        if (tool.IsNull()) {
            return {};
        }
        if (!TopExp_Explorer(tool, TopAbs_SOLID).More()) {
            continue;  // the whole tool was outside the envelope
        }
        (corner.convex ? cutShapes : fuseShapes).emplace_back(tool);
    }

    TopoShape result = base;
    if (!fuseShapes.empty()) {
        fuseShapes.insert(fuseShapes.begin(), result);
        result = TopoShape(0, base.Hasher).makeElementFuse(fuseShapes, op);
    }
    if (cutShapes.size() > 1) {
        cutShapes.front() = result;
        result = TopoShape(0, base.Hasher).makeElementCut(cutShapes, op);
    }
    if (!isDressUpResultValid(base, result)) {
        return {};
    }
    return result;
}

namespace
{

// Region between the corner and the arc of radius R tangent to both faces
TopoDS_Shape makeFilletTool(const DressUpCorner& corner, double radius)
{
    const double half = corner.u1.Angle(corner.u2) / 2;
    const gp_Vec bisector = gp_Vec(gp_Dir(gp_Vec(corner.u1) + gp_Vec(corner.u2)));
    const gp_Vec dir(corner.dir);
    // convex edges: extend past the ends (the tool cuts only air there)
    const double ext = corner.convex ? 0.1 * radius : 0.0;
    if (corner.circular) {
        // section at the start, bounded by the arc, revolved around the axis
        const gp_Pnt p = corner.start;
        const gp_Pnt outer = p.Translated(-bisector * (0.1 * radius));
        const gp_Pnt t1 = p.Translated(gp_Vec(corner.u1) * (radius / std::tan(half)));
        const gp_Pnt t2 = p.Translated(gp_Vec(corner.u2) * (radius / std::tan(half)));
        const gp_Pnt center = p.Translated(bisector * (radius / std::sin(half)));
        const gp_Pnt mid = center.Translated(-bisector * radius);
        GC_MakeArcOfCircle arc(t1, mid, t2);
        if (!arc.IsDone()) {
            return {};
        }
        BRepBuilderAPI_MakeWire wire(
            BRepBuilderAPI_MakeEdge(outer, t1).Edge(),
            BRepBuilderAPI_MakeEdge(arc.Value()).Edge(),
            BRepBuilderAPI_MakeEdge(t2, outer).Edge()
        );
        if (!wire.IsDone()) {
            return {};
        }
        BRepBuilderAPI_MakeFace face(wire.Wire(), Standard_True);
        if (!face.IsDone()) {
            return {};
        }
        return sweepDressUpProfile(corner, face.Face(), ext);
    }
    const gp_Pnt p = corner.start.Translated(-dir * ext);
    // move the corner out of the wedge, so that the tool overlaps the faces
    const gp_Pnt outer = p.Translated(-bisector * (0.1 * radius));
    const gp_Pnt t1 = p.Translated(gp_Vec(corner.u1) * (radius / std::tan(half)));
    const gp_Pnt t2 = p.Translated(gp_Vec(corner.u2) * (radius / std::tan(half)));
    const gp_Pnt center = p.Translated(bisector * (radius / std::sin(half)));
    const double height = corner.length + 2 * ext;

    BRepBuilderAPI_MakePolygon polygon(outer, t1, center, t2, Standard_True);
    BRepBuilderAPI_MakeFace face(polygon.Wire(), Standard_True);
    BRepPrimAPI_MakePrism prism(face.Face(), dir * height);
    BRepPrimAPI_MakeCylinder cylinder(gp_Ax2(center, corner.dir), radius, height);
    BRepAlgoAPI_Cut cut(prism.Shape(), cylinder.Shape());
    if (!cut.IsDone()) {
        return {};
    }
    return cut.Shape();
}

}  // namespace


PROPERTY_SOURCE(PartDesign::Fillet, PartDesign::DressUp)

const App::PropertyQuantityConstraint::Constraints floatRadius
    = {0.0, std::numeric_limits<float>::max(), 0.1};

Fillet::Fillet()
{
    ADD_PROPERTY_TYPE(Radius, (1.0), "Fillet", App::Prop_None, "Fillet radius.");
    Radius.setUnit(Base::Unit::Length);
    Radius.setConstraints(&floatRadius);
    ADD_PROPERTY_TYPE(
        UseAllEdges,
        (false),
        "Fillet",
        App::Prop_None,
        "Fillet all edges if true, else use only those edges in Base property.\n"
        "If true, then this overrides any edge changes made to the Base property or in the "
        "dialog.\n"
    );
    ADD_PROPERTY_TYPE(
        SizeMode,
        (0L),
        "Fillet",
        App::Prop_None,
        "Dimension: use Radius.\n"
        "UpTo: compute the radius so that the fillet reaches the nearest of UpToReferences."
    );
    SizeMode.setEnums(DressUpSizeModeEnums);
    ADD_PROPERTY_TYPE(
        UpToReferences,
        (nullptr),
        "Fillet",
        App::Prop_None,
        "Faces, edges or vertices that the fillet reaches in UpTo mode"
    );
}

void Fillet::onChanged(const App::Property* prop)
{
    if (prop == &SizeMode) {
        // in UpTo mode the radius is computed
        Radius.setStatus(App::Property::ReadOnly, SizeMode.getValue() == 1);
    }
    DressUp::onChanged(prop);
}

short Fillet::mustExecute() const
{
    if (Placement.isTouched() || Radius.isTouched() || SizeMode.isTouched()
        || UpToReferences.isTouched()) {
        return 1;
    }
    return DressUp::mustExecute();
}

App::DocumentObjectExecReturn* Fillet::execute()
{
    if (onlyHaveRefined()) {
        return App::DocumentObject::StdReturn;
    }


    Part::TopoShape baseShape;
    try {
        baseShape = getBaseTopoShape();
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    const TopLoc_Location baseLocation = baseShape.getShape().Location();
    baseShape.setTransform(Base::Matrix4D());

    auto edges = UseAllEdges.getValue() ? baseShape.getSubTopoShapes(TopAbs_EDGE)
                                        : getContinuousEdges(baseShape);
    if (edges.empty()) {
        return new App::DocumentObjectExecReturn(
            QT_TRANSLATE_NOOP("Exception", "Fillet not possible on selected shapes")
        );
    }

    double radius = Radius.getValue();
    if (SizeMode.getValue() == 1) {
        try {
            radius = computeUpToSize(
                baseShape,
                edges,
                getUpToReferenceShapes(UpToReferences, baseLocation),
                [](const DressUpCorner& corner, double distance) {
                    // the tangent line of the fillet lies on the reference
                    return distance * std::tan(corner.u1.Angle(corner.u2) / 2);
                }
            );
        }
        catch (Base::Exception& e) {
            return new App::DocumentObjectExecReturn(e.what());
        }
        // show the computed radius (purged with the other touched properties by the recompute)
        if (std::abs(Radius.getValue() - radius) > Precision::Confusion()) {
            Radius.setValue(radius);
        }
    }

    if (radius <= 0) {
        return new App::DocumentObjectExecReturn(
            QT_TRANSLATE_NOOP("Exception", "Fillet radius must be greater than zero")
        );
    }

    this->positionByBaseFeature();

    try {
        TopoShape shape(0);  //,getDocument()->getStringHasher());

        // Add signal handler for segfault protection
#if defined(__GNUC__) && defined(FC_OS_LINUX)
        Base::SignalException se;
#endif

        usedBooleanFallback = false;
        std::string occError;
        try {
            shape.makeElementFillet(baseShape, edges, radius, radius);
        }
        catch (Standard_Failure& e) {
            occError = e.GetMessageString();
            shape = TopoShape(0);
        }
        catch (Base::Exception& e) {
            occError = e.what();
            shape = TopoShape(0);
        }

        // OCC fails when the fillet consumes a whole face: try the boolean fallback
        if (!isDressUpResultValid(baseShape, shape)) {
            TopoShape fallback = makeDressUpByBoolean(
                baseShape,
                edges,
                [this, radius](const DressUpCorner& corner) {
                    const double tangent = 1.0 / std::tan(corner.u1.Angle(corner.u2) / 2);
                    const double factor = corner.sizeFactor(radius * tangent, radius * tangent);
                    if (factor < 1.0) {
                        Base::Console().warning(
                            "{}: radius of Edge{} reduced to {:.4g} so that the fillet does not "
                            "pass a curved face\n",
                            getFullName(),
                            corner.edgeIndex,
                            radius * factor
                        );
                    }
                    return makeFilletTool(corner, radius * factor);
                },
                Part::OpCodes::Fillet
            );
            if (!fallback.isNull()) {
                Base::Console().log("{}: fillet made with the boolean fallback\n", getFullName());
                shape = fallback;
                usedBooleanFallback = true;
            }
            else if (!occError.empty()) {
                return new App::DocumentObjectExecReturn(occError);
            }
        }
        if (shape.isNull()) {
            return new App::DocumentObjectExecReturn(
                QT_TRANSLATE_NOOP("Exception", "Resulting shape is null")
            );
        }

        TopTools_ListOfShape aLarg;
        aLarg.Append(baseShape.getShape());
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
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
    catch (Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
    catch (...) {
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP(
            "Exception",
            "Fillet operation failed. The selected edges may contain geometry that cannot be "
            "filleted together. "
            "Try filleting edges individually or with a smaller radius."
        ));
    }
}

void Fillet::Restore(Base::XMLReader& reader)
{
    DressUp::Restore(reader);
}

void Fillet::handleChangedPropertyType(Base::XMLReader& reader, const char* TypeName, App::Property* prop)
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

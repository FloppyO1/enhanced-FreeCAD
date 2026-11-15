// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2022 Boyer Pierre-Louis <pierrelouis.boyer@gmail.com>   *
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

#include <FCConfig.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include <QApplication>

#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BSplCLib.hxx>
#include <Bnd_Box.hxx>
#include <ElCLib.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_OffsetCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_ApproxCurve.hxx>
#include <math_Gauss.hxx>
#include <math_Matrix.hxx>
#include <math_Vector.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Pln.hxx>

#include <Base/Exception.h>

#include <Gui/BitmapFactory.h>
#include <Gui/Notifications.h>
#include <Gui/Command.h>
#include <Gui/CommandT.h>

#include <Mod/Sketcher/App/SketchObject.h>

#include <Mod/Sketcher/App/GeometryFacade.h>

#include "DrawSketchDefaultWidgetController.h"
#include "DrawSketchControllableHandler.h"

#include "Utils.h"


using namespace Sketcher;

namespace SketcherGui
{

class DrawSketchHandlerOffset;

namespace ConstructionMethods
{

enum class OffsetConstructionMethod
{
    Arc,
    // Tangent,
    Intersection,
    End  // Must be the last one
};

/* OCC offer various modes as follows, but we use only Arc and Intersection as the rest are buggy.
enum class JoinMode {
    Arc,
    Tangent,
    Intersection
};
//We use Pipe by default only. Skin is buggy.
enum class ModeEnums {
    Skin,
    Pipe,
    RectoVerso
};*/
}  // namespace ConstructionMethods

using DSHOffsetController = DrawSketchDefaultWidgetController<
    DrawSketchHandlerOffset,
    StateMachines::OneSeekEnd,
    /*PAutoConstraintSize =*/0,
    /*OnViewParametersT =*/OnViewParameters<1, 1>,
    /*WidgetParametersT =*/WidgetParameters<0, 0>,
    /*WidgetCheckboxesT =*/WidgetCheckboxes<2, 2>,
    /*WidgetComboboxesT =*/WidgetComboboxes<1, 1>,
    /*WidgetLineEditsT =*/WidgetLineEdits<0, 0>,
    ConstructionMethods::OffsetConstructionMethod,
    /*bool PFirstComboboxIsConstructionMethod =*/true>;

using DSHOffsetControllerBase = DSHOffsetController::ControllerBase;

using DrawSketchHandlerOffsetBase = DrawSketchControllableHandler<DSHOffsetController>;

class DrawSketchHandlerOffset: public DrawSketchHandlerOffsetBase
{
    Q_DECLARE_TR_FUNCTIONS(SketcherGui::DrawSketchHandlerOffset)

    friend DSHOffsetController;
    friend DSHOffsetControllerBase;

public:
    DrawSketchHandlerOffset(
        std::vector<int> listOfGeoIds,
        ConstructionMethod constrMethod = ConstructionMethod::Arc
    )
        : DrawSketchHandlerOffsetBase(constrMethod)
        , listOfGeoIds(listOfGeoIds)
        , deleteOriginal(false)
        , offsetLengthSet(false)
        , offsetConstraint(false)
        , onlySingleLines(true)
        , offsetLength(1.)
    {}

    ~DrawSketchHandlerOffset() override = default;


private:
    void updateDataAndDrawToPosition(Base::Vector2d onSketchPos) override
    {
        if (state() == SelectMode::SeekFirst) {
            endpoint = onSketchPos;

            if (!offsetLengthSet) {
                findOffsetLength();
                toolWidgetManager.drawDoubleAtCursor(onSketchPos, offsetLength);
            }

            if (fabs(offsetLength) > Precision::Confusion()) {
                drawOffsetPreview();
            }
        }
    }

    void executeCommands() override
    {
        if (fabs(offsetLength) > Precision::Confusion()) {
            createOffset();
        }
    }

    std::string getToolName() const override
    {
        return "DSH_Offset";
    }

    QString getCrosshairCursorSVGName() const override
    {
        return QStringLiteral("Sketcher_Pointer_Create_Offset");
    }

    std::unique_ptr<QWidget> createWidget() const override
    {
        return std::make_unique<SketcherToolDefaultWidget>();
    }

    bool isWidgetVisible() const override
    {
        return true;
    };

    QPixmap getToolIcon() const override
    {
        return Gui::BitmapFactory().pixmap("Sketcher_Offset");
    }

    QString getToolWidgetText() const override
    {
        return QString(tr("Offset Parameters"));
    }

    void activated() override
    {
        DrawSketchDefaultHandler::activated();
        continuousMode = false;
        firstCurveCreated = getHighestCurveIndex() + 1;

        generateSourceWires();
    }

public:
    std::list<Gui::InputHint> getToolHints() const override
    {
        using enum Gui::InputHint::UserInput;

        return {
            {tr("%1 set offset direction and distance", "Sketcher Offset: hint"), {MouseLeft}},
        };
    }

private:
    class CoincidencePointPos
    {
    public:
        PointPos firstPos1;
        PointPos secondPos1;
        PointPos firstPos2;
        PointPos secondPos2;
    };

    std::vector<int> listOfGeoIds;
    std::vector<std::vector<int>> vCC;
    std::vector<std::vector<int>> vCCO;
    Base::Vector2d endpoint, pointOnSourceWire;
    std::vector<TopoDS_Wire> sourceWires;

    bool deleteOriginal, offsetLengthSet, offsetConstraint, onlySingleLines;
    double offsetLength;
    int firstCurveCreated;

    TopoDS_Shape makeOffsetShape(bool allowOpenResult = false)
    {
        // in OCC the JointTypes are : Arc(0), Tangent(1), Intersection(2)
        short joinType = constructionMethod() == DrawSketchHandlerOffset::ConstructionMethod::Arc
            ? 0
            : 2;

        // Offset will fail for single lines if we don't set a plane in ctor.
        // But if we set a plane, then the direction of offset is forced...
        // so we set a plane if and only if there are not a single sourceWires with more than single
        // line.
        BRepOffsetAPI_MakeOffset mkOffset;

        if (onlySingleLines) {
            TopoDS_Face workingPlane = BRepBuilderAPI_MakeFace(gp_Pln(gp::Origin(), gp::DZ()));
            mkOffset = BRepOffsetAPI_MakeOffset(workingPlane);
        }
        mkOffset.Init(GeomAbs_JoinType(joinType), allowOpenResult);

        for (TopoDS_Wire& wire : sourceWires) {
            mkOffset.AddWire(wire);
        }
        try {
#if defined(__GNUC__) && defined(FC_OS_LINUX)
            Base::SignalException se;
#endif
            mkOffset.Perform(offsetLength);
        }
        catch (Standard_Failure&) {
            throw;
        }
        catch (...) {
            throw Base::CADKernelError(
                "BRepOffsetAPI_MakeOffset has crashed! (Unknown exception caught)"
            );
        }

        TopoDS_Shape offsetShape = mkOffset.Shape();

        if (offsetShape.IsNull()) {
            return offsetShape;
        }

        // Copying shape to fix strange orientation behavior, OCC7.0.0. See bug #2699
        //  http://www.freecad.org/tracker/view.php?id=2699
        offsetShape = BRepBuilderAPI_Copy(offsetShape).Shape();
        return offsetShape;
    }

    Part::Geometry* curveToLine(BRepAdaptor_Curve curve)
    {
        double first = curve.FirstParameter();
        if (fabs(first) > 1E99) {
            first = -10000;
        }

        double last = curve.LastParameter();
        if (fabs(last) > 1E99) {
            last = +10000;
        }

        gp_Pnt P1 = curve.Value(first);
        gp_Pnt P2 = curve.Value(last);

        Base::Vector3d p1(P1.X(), P1.Y(), P1.Z());
        Base::Vector3d p2(P2.X(), P2.Y(), P2.Z());
        auto* line = new Part::GeomLineSegment();
        line->setPoints(p1, p2);
        GeometryFacade::setConstruction(line, false);
        return line;
    }

    Part::Geometry* curveToCircleOrArc(BRepAdaptor_Curve curve, const TopoDS_Edge& /*edge*/)
    {
        gp_Circ circle = curve.Circle();
        gp_Pnt cnt = circle.Location();
        gp_Pnt beg = curve.Value(curve.FirstParameter());
        gp_Pnt end = curve.Value(curve.LastParameter());

        if (beg.SquareDistance(end) < Precision::Confusion()) {
            auto* gCircle = new Part::GeomCircle();
            gCircle->setRadius(circle.Radius());
            gCircle->setCenter(Base::Vector3d(cnt.X(), cnt.Y(), cnt.Z()));

            GeometryFacade::setConstruction(gCircle, false);
            return gCircle;
        }
        else {
            Handle(Geom_Circle) hCircle = new Geom_Circle(circle);

            double u1 = curve.FirstParameter();
            double u2 = curve.LastParameter();

            auto* gArc = new Part::GeomArcOfCircle();
            Handle(Geom_TrimmedCurve) tCurve = new Geom_TrimmedCurve(hCircle, u1, u2);
            gArc->setHandle(tCurve);

            gArc->reverseIfReversed();

            GeometryFacade::setConstruction(gArc, false);
            return gArc;
        }
    }

    Part::Geometry* curveToEllipseOrArc(BRepAdaptor_Curve curve, const TopoDS_Edge& /*edge*/)
    {
        gp_Elips ellipse = curve.Ellipse();
        gp_Pnt beg = curve.Value(curve.FirstParameter());
        gp_Pnt end = curve.Value(curve.LastParameter());

        if (beg.SquareDistance(end) < Precision::Confusion()) {
            auto* gEllipse = new Part::GeomEllipse();
            Handle(Geom_Ellipse) hEllipse = new Geom_Ellipse(ellipse);

            gEllipse->setHandle(hEllipse);

            gEllipse->reverseIfReversed();

            GeometryFacade::setConstruction(gEllipse, false);
            return gEllipse;
        }
        else {
            Handle(Geom_Ellipse) hEllipse = new Geom_Ellipse(ellipse);

            double u1 = curve.FirstParameter();
            double u2 = curve.LastParameter();

            Handle(Geom_TrimmedCurve) tCurve = new Geom_TrimmedCurve(hEllipse, u1, u2);
            auto* gArc = new Part::GeomArcOfEllipse();
            gArc->setHandle(tCurve);

            gArc->reverseIfReversed();

            GeometryFacade::setConstruction(gArc, false);
            return gArc;
        }
    }

    // Free-form offset edges (offsets of B-splines) are converted to B-splines. OCC splits the
    // offset of one curve into several edges, so the pieces of the same offset curve are joined
    // first, and each joined piece becomes one B-spline.
    struct OffsetPiece
    {
        Handle(Geom_Curve) carrier;  // untrimmed curve the piece lies on
        double first;
        double last;
        bool reversed;
    };

    static Handle(Geom_Curve) stripTrimmed(Handle(Geom_Curve) curve)
    {
        while (curve->IsKind(STANDARD_TYPE(Geom_TrimmedCurve))) {
            curve = Handle(Geom_TrimmedCurve)::DownCast(curve)->BasisCurve();
        }
        return curve;
    }

    static bool isSameCarrier(const Handle(Geom_Curve)& curve1, const Handle(Geom_Curve)& curve2)
    {
        if (curve1 == curve2) {
            return true;
        }
        auto offset1 = Handle(Geom_OffsetCurve)::DownCast(curve1);
        auto offset2 = Handle(Geom_OffsetCurve)::DownCast(curve2);
        if (!offset1.IsNull() && !offset2.IsNull()) {
            return std::fabs(offset1->Offset() - offset2->Offset()) < Precision::Confusion()
                && offset1->Direction().IsEqual(offset2->Direction(), Precision::Angular())
                && isSameCarrier(
                       stripTrimmed(offset1->BasisCurve()),
                       stripTrimmed(offset2->BasisCurve())
                );
        }
        auto bspline1 = Handle(Geom_BSplineCurve)::DownCast(curve1);
        auto bspline2 = Handle(Geom_BSplineCurve)::DownCast(curve2);
        if (bspline1.IsNull() || bspline2.IsNull() || bspline1->Degree() != bspline2->Degree()
            || bspline1->NbPoles() != bspline2->NbPoles()
            || bspline1->NbKnots() != bspline2->NbKnots()
            || bspline1->IsPeriodic() != bspline2->IsPeriodic()) {
            return false;
        }
        for (int i = 1; i <= bspline1->NbPoles(); i++) {
            if (bspline1->Pole(i).Distance(bspline2->Pole(i)) > Precision::Confusion()
                || std::fabs(bspline1->Weight(i) - bspline2->Weight(i)) > Precision::PConfusion()) {
                return false;
            }
        }
        for (int i = 1; i <= bspline1->NbKnots(); i++) {
            if (std::fabs(bspline1->Knot(i) - bspline2->Knot(i)) > Precision::PConfusion()
                || bspline1->Multiplicity(i) != bspline2->Multiplicity(i)) {
                return false;
            }
        }
        return true;
    }

    // Two pieces of an offset curve can be joined at u only where its basis curve is not a corner.
    static bool isSmoothAt(const Handle(Geom_Curve)& carrier, double u)
    {
        Handle(Geom_Curve) basis = carrier;
        if (auto offset = Handle(Geom_OffsetCurve)::DownCast(carrier)) {
            basis = stripTrimmed(offset->BasisCurve());
        }
        auto bspline = Handle(Geom_BSplineCurve)::DownCast(basis);
        if (bspline.IsNull()) {
            return true;
        }
        if (bspline->IsPeriodic()) {
            bspline->PeriodicNormalization(u);
        }
        int index1 = 0;
        int index2 = 0;
        bspline->LocateU(u, Precision::PConfusion(), index1, index2);
        return index1 != index2 || bspline->Multiplicity(index1) < bspline->Degree();
    }

    static bool edgeToOffsetPiece(const TopoDS_Edge& edge, std::vector<OffsetPiece>& pieces)
    {
        double first {};
        double last {};
        Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) {
            return false;
        }
        Handle(Geom_Curve) carrier = stripTrimmed(curve);
        if (auto offset = Handle(Geom_OffsetCurve)::DownCast(carrier)) {
            // The pieces of one offset curve can lie on different trims of the same basis curve.
            Handle(Geom_Curve) basis = stripTrimmed(offset->BasisCurve());
            if (basis != offset->BasisCurve()) {
                carrier = new Geom_OffsetCurve(basis, offset->Offset(), offset->Direction());
            }
        }
        pieces.push_back({carrier, first, last, edge.Orientation() == TopAbs_REVERSED});
        return true;
    }

    static void mergeOffsetPieces(std::vector<OffsetPiece>& pieces)
    {
        bool merged = true;
        while (merged) {
            merged = false;
            for (size_t i = 0; i < pieces.size() && !merged; i++) {
                for (size_t j = 0; j < pieces.size() && !merged; j++) {
                    OffsetPiece& piece = pieces[i];
                    const OffsetPiece& next = pieces[j];
                    if (i == j || piece.reversed != next.reversed
                        || !isSameCarrier(piece.carrier, next.carrier)) {
                        continue;
                    }
                    double gap = next.first - piece.last;
                    double period = piece.carrier->IsPeriodic() ? piece.carrier->Period() : 0.;
                    if (period > 0.) {
                        gap -= period * std::round(gap / period);
                    }
                    double newLast = piece.last + (next.last - next.first);
                    if (std::fabs(gap) > Precision::PConfusion()
                        || (period > 0. && newLast - piece.first > period + Precision::PConfusion())
                        || !isSmoothAt(piece.carrier, piece.last)) {
                        continue;
                    }
                    piece.last = newLast;
                    pieces.erase(pieces.begin() + j);
                    merged = true;
                }
            }
        }
    }

    // Moves u to the parameter of the point of the curve nearest to point (local search).
    static double projectOnCurve(const Handle(Geom_Curve)& curve, const gp_Pnt& point, double u)
    {
        for (int i = 0; i < 10; i++) {
            gp_Pnt p;
            gp_Vec d1, d2;
            curve->D2(u, p, d1, d2);
            gp_Vec diff(point, p);
            double den = d1.SquareMagnitude() + diff.Dot(d2);
            if (std::fabs(den) < gp::Resolution()) {
                break;
            }
            double step = diff.Dot(d1) / den;
            u -= step;
            if (std::fabs(step) < Precision::PConfusion()) {
                break;
            }
        }
        return u;
    }

    // Least squares fit of points at params, with the knots of shape. The end poles of a non
    // periodic shape are fixed to start and end.
    static Handle(Geom_BSplineCurve) fitPoints(
        const Handle(Geom_BSplineCurve)& shape,
        const std::vector<double>& params,
        const std::vector<gp_Pnt>& points,
        const gp_Pnt& start,
        const gp_Pnt& end
    )
    {
        const bool periodic = shape->IsPeriodic();
        const int degree = shape->Degree();
        const int nPoles = shape->NbPoles();
        NCollection_Array1<double> flatKnots(
            1,
            BSplCLib::KnotSequenceLength(shape->Multiplicities(), degree, periodic)
        );
        shape->KnotSequence(flatKnots);

        const int firstFree = periodic ? 1 : 2;
        const int nFree = periodic ? nPoles : nPoles - 2;
        NCollection_Array1<gp_Pnt> poles(1, nPoles);
        poles(1) = start;
        poles(nPoles) = end;
        if (nFree > 0) {
            math_Matrix normal(1, nFree, 1, nFree, 0.0);
            math_Vector rhs[3] = {
                math_Vector(1, nFree, 0.0),
                math_Vector(1, nFree, 0.0),
                math_Vector(1, nFree, 0.0)
            };
            math_Matrix basis(1, 1, 1, degree + 1);
            std::vector<int> index(degree + 1);
            std::vector<double> value(degree + 1);
            for (size_t s = 0; s < params.size(); s++) {
                int firstNonZero = 0;
                BSplCLib::EvalBsplineBasis(
                    0,
                    degree + 1,
                    flatKnots,
                    params[s],
                    firstNonZero,
                    basis,
                    periodic
                );
                gp_XYZ target = points[s].XYZ();
                for (int j = 0; j <= degree; j++) {
                    int pole = firstNonZero + j;
                    if (periodic) {
                        pole = (pole - 1) % nPoles + 1;
                    }
                    value[j] = basis(1, j + 1);
                    index[j] = pole - firstFree + 1;
                    if (!periodic && pole == 1) {
                        target -= value[j] * start.XYZ();
                    }
                    else if (!periodic && pole == nPoles) {
                        target -= value[j] * end.XYZ();
                    }
                }
                for (int a = 0; a <= degree; a++) {
                    if (index[a] < 1 || index[a] > nFree) {
                        continue;
                    }
                    for (int b = 0; b <= degree; b++) {
                        if (index[b] >= 1 && index[b] <= nFree) {
                            normal(index[a], index[b]) += value[a] * value[b];
                        }
                    }
                    for (int c = 0; c < 3; c++) {
                        rhs[c](index[a]) += value[a] * target.Coord(c + 1);
                    }
                }
            }
            math_Gauss solver(normal);
            if (!solver.IsDone()) {
                return {};
            }
            for (int c = 0; c < 3; c++) {
                math_Vector x(1, nFree);
                solver.Solve(rhs[c], x);
                for (int i = 1; i <= nFree; i++) {
                    poles(i + firstFree - 1).SetCoord(c + 1, x(i));
                }
            }
        }
        Handle(Geom_BSplineCurve) fit = Handle(Geom_BSplineCurve)::DownCast(shape->Copy());
        for (int i = 1; i <= nPoles; i++) {
            fit->SetPole(i, poles(i));
        }
        return fit;
    }

    // Approximates target over the parameter range of shape. The fit starts with the knots of
    // shape (the knots of the curve that was offset) and adds knots where the distance to target
    // exceeds tolerance, so the number of poles stays close to the one of the original curve.
    static Handle(Geom_BSplineCurve) fitOnKnots(
        const Handle(Geom_Curve)& target,
        Handle(Geom_BSplineCurve) shape,
        double tolerance
    )
    {
        const int maxPoles = 500;
        const double first = shape->FirstParameter();
        const double last = shape->LastParameter();
        const gp_Pnt start = target->Value(first);
        const gp_Pnt end = target->Value(last);
        for (int iteration = 0; iteration < 60; iteration++) {
            const int samplesPerSpan = shape->Degree() + 4;
            std::vector<double> sampleParams;  // parameters on target
            std::vector<double> params;        // parameters on the fit
            std::vector<gp_Pnt> points;
            for (int k = 1; k < shape->NbKnots(); k++) {
                double k0 = shape->Knot(k);
                double k1 = shape->Knot(k + 1);
                for (int s = 0; s < samplesPerSpan; s++) {
                    params.push_back(k0 + (k1 - k0) * (s + 0.5) / samplesPerSpan);
                    points.push_back(target->Value(params.back()));
                    sampleParams.push_back(params.back());
                }
            }

            // Fit, then move the parameters to the feet of the points on the fit and fit again,
            // so that the fit follows the shape of target rather than its parametrization.
            Handle(Geom_BSplineCurve) fit;
            for (int pass = 0; pass < 4; pass++) {
                fit = fitPoints(shape, params, points, start, end);
                if (fit.IsNull()) {
                    return {};
                }
                if (pass == 3) {
                    break;
                }
                for (size_t s = 0; s < params.size(); s++) {
                    double u = projectOnCurve(fit, points[s], params[s]);
                    if (shape->IsPeriodic()) {
                        u = ElCLib::InPeriod(u, first, last);
                    }
                    params[s] = std::clamp(u, first, last);
                }
            }

            // Distance from target to the fit, at the samples and between them, gathered per span
            // of the fit. Knots are added in the worst spans only, as each new knot also improves
            // its neighbours.
            std::vector<double> spanError(shape->NbKnots() - 1, 0.);
            auto addError = [&](double u, double guess) {
                gp_Pnt p = target->Value(u);
                double t = projectOnCurve(fit, p, guess);
                t = shape->IsPeriodic() ? ElCLib::InPeriod(t, first, last)
                                        : std::clamp(t, first, last);
                int span = 0;
                int next = 0;
                shape->LocateU(t, Precision::PConfusion(), span, next);
                span = std::clamp(span, 1, shape->NbKnots() - 1);
                spanError[span - 1] = std::max(spanError[span - 1], p.Distance(fit->Value(t)));
            };
            for (size_t s = 0; s < params.size(); s++) {
                addError(sampleParams[s], params[s]);
                if (s + 1 < params.size()) {
                    bool wraps = std::fabs(params[s + 1] - params[s]) > 0.5 * (last - first);
                    addError(
                        0.5 * (sampleParams[s] + sampleParams[s + 1]),
                        wraps ? params[s] : 0.5 * (params[s] + params[s + 1])
                    );
                }
            }
            double maxError = *std::max_element(spanError.begin(), spanError.end());
            std::vector<double> newKnots;
            for (int k = 1; k < shape->NbKnots(); k++) {
                if (spanError[k - 1] > tolerance && spanError[k - 1] >= 0.5 * maxError) {
                    newKnots.push_back(0.5 * (shape->Knot(k) + shape->Knot(k + 1)));
                }
            }
            if (newKnots.empty()) {
                return fit;
            }
            if (shape->NbPoles() + static_cast<int>(newKnots.size()) > maxPoles) {
                return {};
            }
            for (double u : newKnots) {
                shape->InsertKnot(u, 1, Precision::PConfusion());
            }
        }
        return {};
    }

    static Handle(Geom_BSplineCurve) offsetPieceToBSpline(const OffsetPiece& piece, double tolerance)
    {
        const Handle(Geom_Curve)& carrier = piece.carrier;
        const bool fullLoop = carrier->IsPeriodic()
            && std::fabs(piece.last - piece.first - carrier->Period()) < Precision::PConfusion();

        Handle(Geom_BSplineCurve) bspline;
        if (carrier->IsKind(STANDARD_TYPE(Geom_BSplineCurve))
            || carrier->IsKind(STANDARD_TYPE(Geom_BezierCurve))) {
            // Exact conversion.
            if (fullLoop) {
                bspline = Handle(Geom_BSplineCurve)::DownCast(carrier->Copy());
            }
            else {
                Handle(Geom_Curve) trimmed = new Geom_TrimmedCurve(carrier, piece.first, piece.last);
                bspline = GeomConvert::CurveToBSplineCurve(trimmed);
            }
        }
        else {
            Handle(Geom_BSplineCurve) basis;
            if (auto offset = Handle(Geom_OffsetCurve)::DownCast(carrier)) {
                basis = Handle(Geom_BSplineCurve)::DownCast(stripTrimmed(offset->BasisCurve()));
            }
            if (!basis.IsNull()
                && (basis->IsPeriodic()
                    || (piece.first > basis->FirstParameter() - Precision::PConfusion()
                        && piece.last < basis->LastParameter() + Precision::PConfusion()))) {
                Handle(Geom_BSplineCurve) shape = Handle(Geom_BSplineCurve)::DownCast(basis->Copy());
                if (!fullLoop) {
                    shape->Segment(piece.first, piece.last);
                }
                // Only the knots of shape are used: the fit is polynomial.
                if (shape->IsRational()) {
                    for (int i = 1; i <= shape->NbPoles(); i++) {
                        shape->SetWeight(i, 1.0);
                    }
                }
                if (shape->Degree() < 3) {
                    shape->IncreaseDegree(3);
                }
                bspline = fitOnKnots(carrier, shape, tolerance);
            }
            if (bspline.IsNull()) {
                // Generic approximation, for other kinds of curves.
                Handle(Geom_Curve) trimmed = new Geom_TrimmedCurve(carrier, piece.first, piece.last);
                GeomConvert_ApproxCurve approx(trimmed, tolerance, GeomAbs_C2, 200, 9);
                if (!approx.HasResult() || approx.MaxError() > 10 * tolerance) {
                    return {};
                }
                bspline = approx.Curve();
                // Exact end points, so that the joints with the neighbours are found.
                gp_Pnt start = trimmed->Value(trimmed->FirstParameter());
                bspline->SetPole(1, start);
                bspline->SetPole(
                    bspline->NbPoles(),
                    fullLoop ? start : trimmed->Value(trimmed->LastParameter())
                );
            }
        }

        if (piece.reversed) {
            bspline->Reverse();
        }
        return bspline;
    }

    // Finds where the curve crosses itself near the parameters u and v.
    static bool intersectBranches(const Handle(Geom_Curve)& curve, double& u, double& v)
    {
        const double u0 = u;
        const double v0 = v;
        for (int i = 0; i < 50; i++) {
            gp_Pnt p, q;
            gp_Vec du, dv;
            curve->D1(u, p, du);
            curve->D1(v, q, dv);
            gp_Vec diff(q, p);
            if (diff.Magnitude() < Precision::Confusion() * 1e-3) {
                break;
            }
            // Solve du * a - dv * b = -diff in the sketch plane.
            double det = dv.X() * du.Y() - du.X() * dv.Y();
            if (std::fabs(det) < gp::Resolution()) {
                return false;
            }
            u += (diff.X() * dv.Y() - dv.X() * diff.Y()) / det;
            v += (diff.X() * du.Y() - du.X() * diff.Y()) / det;
        }
        double range = curve->IsPeriodic() ? curve->Period()
                                           : curve->LastParameter() - curve->FirstParameter();
        return curve->Value(u).Distance(curve->Value(v)) < Precision::Confusion()
            && std::fabs(u - u0) < 0.05 * range && std::fabs(v - v0) < 0.05 * range;
    }

    // Parameters (u, v) where the closed curve crosses itself. Found on a polyline through
    // nSamples points of the curve, then refined on the curve.
    static std::vector<std::pair<double, double>> findSelfIntersections(
        const Handle(Geom_Curve)& curve,
        double first,
        double period,
        int nSamples
    )
    {
        struct Segment
        {
            int index;
            double xMin;
            double xMax;
            double yMin;
            double yMax;
        };
        const double step = period / nSamples;
        std::vector<gp_Pnt> points;
        for (int i = 0; i <= nSamples; i++) {
            points.push_back(curve->Value(first + step * (i % nSamples)));
        }
        std::vector<Segment> segments;
        for (int i = 0; i < nSamples; i++) {
            const gp_Pnt& p = points[i];
            const gp_Pnt& q = points[i + 1];
            segments.push_back(
                {i,
                 std::min(p.X(), q.X()),
                 std::max(p.X(), q.X()),
                 std::min(p.Y(), q.Y()),
                 std::max(p.Y(), q.Y())}
            );
        }
        std::sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) {
            return a.xMin < b.xMin;
        });

        std::vector<std::pair<double, double>> intersections;
        for (size_t a = 0; a < segments.size(); a++) {
            for (size_t b = a + 1; b < segments.size() && segments[b].xMin <= segments[a].xMax;
                 b++) {
                const Segment* s1 = &segments[a];
                const Segment* s2 = &segments[b];
                if (s1->index > s2->index) {
                    std::swap(s1, s2);
                }
                int gap = s2->index - s1->index;
                if (gap <= 1 || gap >= nSamples - 1 || s1->yMax < s2->yMin || s2->yMax < s1->yMin) {
                    continue;  // neighbours, or no overlap
                }
                gp_Vec d1(points[s1->index], points[s1->index + 1]);
                gp_Vec d2(points[s2->index], points[s2->index + 1]);
                gp_Vec w(points[s2->index], points[s1->index]);
                double det = d2.X() * d1.Y() - d1.X() * d2.Y();
                if (std::fabs(det) < gp::Resolution()) {
                    continue;
                }
                // Solve points[s1] + r * d1 = points[s2] + t * d2.
                double r = (w.X() * d2.Y() - d2.X() * w.Y()) / det;
                double t = (w.X() * d1.Y() - d1.X() * w.Y()) / det;
                if (r < 0. || r >= 1. || t < 0. || t >= 1.) {
                    continue;
                }
                double u = first + step * (s1->index + r);
                double v = first + step * (s2->index + t);
                if (!intersectBranches(curve, u, v)) {
                    continue;
                }
                u = ElCLib::InPeriod(u, first, first + period);
                v = ElCLib::InPeriod(v, first, first + period);
                bool known = false;
                for (const auto& [ku, kv] : intersections) {
                    known = known
                        || (std::fabs(ku - u) < 1e-9 * period && std::fabs(kv - v) < 1e-9 * period);
                }
                if (!known) {
                    intersections.emplace_back(u, v);
                }
            }
        }
        return intersections;
    }

    // Twice the signed area enclosed by the closed curve (positive when counterclockwise).
    static double signedArea(const Handle(Geom_Curve)& curve, double first, double period)
    {
        const int nSamples = 1000;
        double area = 0.;
        gp_Pnt previous = curve->Value(first);
        for (int i = 1; i <= nSamples; i++) {
            gp_Pnt p = curve->Value(first + period * (i % nSamples) / nSamples);
            area += previous.X() * p.Y() - p.X() * previous.Y();
            previous = p;
        }
        return area;
    }

    // Exact offset of a closed smooth (periodic) curve, outward for a positive offset. The
    // offset curve is cut where it crosses itself, and the pieces that come closer to the source
    // than the offset distance (the loops at the cusps) are left out.
    static bool offsetClosedCurve(
        const Handle(Geom_BSplineCurve)& source,
        double offset,
        std::vector<OffsetPiece>& pieces
    )
    {
        const double first = source->FirstParameter();
        const double period = source->Period();
        const double distance = std::fabs(offset);
        // The positive side of Geom_OffsetCurve is on the right of the curve.
        bool counterclockwise = signedArea(source, first, period) > 0.;
        double signedOffset = (counterclockwise == (offset > 0.)) ? distance : -distance;
        Handle(Geom_Curve) carrier = new Geom_OffsetCurve(source, signedOffset, gp::DZ());

        int nSamples = std::clamp(32 * source->NbKnots(), 512, 4096);
        std::vector<double> cuts;
        for (const auto& [u, v] : findSelfIntersections(carrier, first, period, nSamples)) {
            cuts.push_back(u);
            cuts.push_back(v);
        }
        std::sort(cuts.begin(), cuts.end());

        auto isValid = [&](double u) {
            GeomAPI_ProjectPointOnCurve projection(carrier->Value(u), source);
            return projection.NbPoints() > 0
                && projection.LowerDistance() > distance * (1. - 1e-6) - Precision::Confusion();
        };

        if (cuts.empty()) {
            if (!isValid(first)) {
                return false;
            }
            pieces.push_back({carrier, first, first + period, false});
            return true;
        }

        // Valid intervals between cuts, joined where two of them follow each other.
        std::vector<std::pair<double, double>> intervals;
        for (size_t i = 0; i < cuts.size(); i++) {
            double start = cuts[i];
            double end = i + 1 < cuts.size() ? cuts[i + 1] : cuts[0] + period;
            if (end - start < Precision::PConfusion()) {
                continue;
            }
            double length = end - start;
            if (!isValid(start + 0.25 * length) || !isValid(start + 0.5 * length)
                || !isValid(start + 0.75 * length)) {
                continue;
            }
            if (!intervals.empty()
                && std::fabs(intervals.back().second - start) < Precision::PConfusion()) {
                intervals.back().second = end;
            }
            else {
                intervals.emplace_back(start, end);
            }
        }
        if (intervals.size() > 1
            && std::fabs(intervals.back().second - (intervals.front().first + period))
                < Precision::PConfusion()) {
            intervals.front().first = intervals.back().first - period;
            intervals.pop_back();
        }
        for (const auto& [start, end] : intervals) {
            pieces.push_back({carrier, start, end, false});
        }
        return !intervals.empty();
    }

    // Size of the source geometry, used to scale the tolerances.
    double getSourceSize() const
    {
        Bnd_Box box;
        for (const TopoDS_Wire& wire : sourceWires) {
            BRepBndLib::Add(wire, box);
        }
        return box.IsVoid() ? 0. : std::sqrt(box.SquareExtent());
    }

    // The periodic B-spline of a source wire made of only that curve, or null.
    static Handle(Geom_BSplineCurve) getPeriodicBSpline(const TopoDS_Wire& wire)
    {
        TopExp_Explorer expl(wire, TopAbs_EDGE);
        if (!expl.More()) {
            return {};
        }
        TopoDS_Edge edge = TopoDS::Edge(expl.Current());
        expl.Next();
        if (expl.More()) {
            return {};
        }
        double first {};
        double last {};
        Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) {
            return {};
        }
        auto bspline = Handle(Geom_BSplineCurve)::DownCast(stripTrimmed(curve));
        if (bspline.IsNull() || !bspline->IsPeriodic()) {
            return {};
        }
        return bspline;
    }

    // OCC approximates the offset of a curve with arcs and lines when the offset has cusps (when
    // the offset distance is larger than the radius of curvature somewhere). The offset wires of
    // a source wire made of one periodic B-spline are then replaced by its exact offset, added to
    // pieces. Returns the offset wires to keep.
    std::vector<TopoDS_Wire> replaceApproximatedWires(
        const TopoDS_Shape& offsetShape,
        std::vector<OffsetPiece>& pieces
    ) const
    {
        std::vector<TopoDS_Wire> offsetWires;
        std::vector<std::vector<TopoDS_Edge>> wireEdges;
        std::vector<bool> approximated;
        for (TopExp_Explorer expl(offsetShape, TopAbs_WIRE); expl.More(); expl.Next()) {
            offsetWires.push_back(TopoDS::Wire(expl.Current()));
            wireEdges.emplace_back();
            approximated.push_back(false);
            for (TopExp_Explorer edgeExpl(offsetWires.back(), TopAbs_EDGE); edgeExpl.More();
                 edgeExpl.Next()) {
                wireEdges.back().push_back(TopoDS::Edge(edgeExpl.Current()));
                GeomAbs_CurveType type = BRepAdaptor_Curve(wireEdges.back().back()).GetType();
                approximated.back() = approximated.back() || type == GeomAbs_Line
                    || type == GeomAbs_Circle;
            }
        }
        if (std::find(approximated.begin(), approximated.end(), true) == approximated.end()) {
            return offsetWires;
        }

        // The source wire each offset wire comes from: the nearest source wire of all its edges
        // (of a sample of them), or -1 if it comes from several (offsets of different wires that
        // merged).
        std::vector<int> wireSource(offsetWires.size(), -1);
        for (size_t i = 0; i < offsetWires.size(); i++) {
            const std::vector<TopoDS_Edge>& edges = wireEdges[i];
            const size_t step = std::max<size_t>(1, edges.size() / 16);
            for (size_t e = 0; e < edges.size(); e += step) {
                BRepAdaptor_Curve curve(edges[e]);
                TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(
                    curve.Value(0.5 * (curve.FirstParameter() + curve.LastParameter()))
                );
                int edgeSource = -1;
                double nearest = std::numeric_limits<double>::max();
                for (size_t j = 0; j < sourceWires.size(); j++) {
                    BRepExtrema_DistShapeShape distTool(sourceWires[j], vertex);
                    if (distTool.IsDone() && distTool.Value() < nearest) {
                        nearest = distTool.Value();
                        edgeSource = static_cast<int>(j);
                    }
                }
                if (e > 0 && edgeSource != wireSource[i]) {
                    wireSource[i] = -1;
                    break;
                }
                wireSource[i] = edgeSource;
            }
        }

        std::vector<bool> replaced(sourceWires.size(), false);
        for (size_t j = 0; j < sourceWires.size(); j++) {
            bool needed = false;
            for (size_t i = 0; i < offsetWires.size(); i++) {
                needed = needed || (approximated[i] && wireSource[i] == static_cast<int>(j));
            }
            Handle(Geom_BSplineCurve) source;
            if (needed) {
                source = getPeriodicBSpline(sourceWires[j]);
            }
            if (source.IsNull()) {
                continue;
            }
            try {
                // A positive offset is outward, as for BRepOffsetAPI_MakeOffset.
                std::vector<OffsetPiece> exact;
                if (offsetClosedCurve(source, offsetLength, exact)) {
                    pieces.insert(pieces.end(), exact.begin(), exact.end());
                    replaced[j] = true;
                }
            }
            catch (const Standard_Failure&) {
            }
        }

        std::vector<TopoDS_Wire> kept;
        for (size_t i = 0; i < offsetWires.size(); i++) {
            if (wireSource[i] < 0 || !replaced[wireSource[i]]) {
                kept.push_back(offsetWires[i]);
            }
        }
        return kept;
    }

    // Returns false if some free-form piece of the offset could not be converted.
    bool getOffsetGeos(std::vector<Part::Geometry*>& geometriesToAdd, std::vector<int>& listOfOffsetGeoIds)
    {
        TopoDS_Shape offsetShape = makeOffsetShape();
        if (offsetShape.IsNull()) {
            return true;
        }

        std::vector<TopoDS_Edge> edges;
        std::vector<OffsetPiece> pieces;
        for (const TopoDS_Wire& wire : replaceApproximatedWires(offsetShape, pieces)) {
            for (TopExp_Explorer expl(wire, TopAbs_EDGE); expl.More(); expl.Next()) {
                edges.push_back(TopoDS::Edge(expl.Current()));
            }
        }
        for (TopExp_Explorer expl(offsetShape, TopAbs_EDGE, TopAbs_WIRE); expl.More(); expl.Next()) {
            edges.push_back(TopoDS::Edge(expl.Current()));
        }

        int geoIdToAdd = firstCurveCreated;
        auto addGeometry = [&](Part::Geometry* geo) {
            geometriesToAdd.push_back(geo);
            listOfOffsetGeoIds.push_back(geoIdToAdd++);
        };

        bool complete = true;
        for (const TopoDS_Edge& edge : edges) {
            BRepAdaptor_Curve curve(edge);
            if (curve.GetType() == GeomAbs_Line) {
                addGeometry(curveToLine(curve));
            }
            else if (curve.GetType() == GeomAbs_Circle) {
                addGeometry(curveToCircleOrArc(curve, edge));
            }
            else if (curve.GetType() == GeomAbs_Ellipse) {
                addGeometry(curveToEllipseOrArc(curve, edge));
            }
            else if (!edgeToOffsetPiece(edge, pieces)) {
                complete = false;
            }
        }

        mergeOffsetPieces(pieces);
        // About a millionth of the size of the geometry, at most 1e-4 mm.
        double tolerance = std::clamp(1e-6 * getSourceSize(), 1e-6, 1e-4);
        for (const OffsetPiece& piece : pieces) {
            Handle(Geom_BSplineCurve) bspline;
            try {
                bspline = offsetPieceToBSpline(piece, tolerance);
            }
            catch (const Standard_Failure&) {
            }
            if (bspline.IsNull()) {
                complete = false;
                continue;
            }
            auto* geo = new Part::GeomBSplineCurve(bspline);
            GeometryFacade::setConstruction(geo, false);
            addGeometry(geo);
        }
        return complete;
    }

    void drawOffsetPreview()
    {
        try {
            std::vector<Part::Geometry*> geometriesToAdd;
            std::vector<int> listOfOffsetGeoIds;
            getOffsetGeos(geometriesToAdd, listOfOffsetGeoIds);

            drawEdit(geometriesToAdd);
        }
        catch (const Base::Exception& e) {
            e.reportException();
        }
    }

    void createOffset()
    {
        std::vector<Part::Geometry*> geometriesToAdd;
        std::vector<int> listOfOffsetGeoIds;
        bool complete = getOffsetGeos(geometriesToAdd, listOfOffsetGeoIds);

        SketchObject* Obj = sketchgui->getSketchObject();

        if (!complete) {
            Base::Console().warning("Offset: some curves of the offset could not be converted\n");
        }

        if (listOfOffsetGeoIds.empty()) {
            Gui::NotifyUserError(
                Obj,
                QT_TRANSLATE_NOOP("Notifications", "Offset Error"),
                QT_TRANSLATE_NOOP("Notifications", "Offset could not be created.")
            );
            return;
        }

        openCommand(QT_TRANSLATE_NOOP("Command", "Offset"));

        // Create geos
        Obj->addGeometry(std::move(geometriesToAdd));

        // Create coincident (& tangent) constraints
        jointOffsetCurves(listOfOffsetGeoIds);

        for (int geoId : listOfOffsetGeoIds) {
            if (isBSplineCurve(*Obj->getGeometry(geoId))) {
                addBSplineInternalGeometry(geoId);
            }
        }

        if (deleteOriginal) {
            deleteOriginalGeometries();
        }
        else if (offsetConstraint) {
            makeOffsetConstraint(listOfOffsetGeoIds);
        }

        commitCommand();
    }

    // Adds the control point circles and the knot points of a new B-spline, as the B-spline tool
    // does. The circles are made with the radius of their weight: exposeInternalGeometry() alone
    // would make them smaller and let the solver fix the radii, and on the way the solver moves
    // the poles away from the computed offset.
    void addBSplineInternalGeometry(int geoId)
    {
        SketchObject* Obj = sketchgui->getSketchObject();
        const auto* bspline = static_cast<const Part::GeomBSplineCurve*>(Obj->getGeometry(geoId));
        std::vector<Base::Vector3d> poles = bspline->getPoles();
        std::vector<double> weights = bspline->getWeights();

        std::vector<Part::Geometry*> circles;
        for (size_t i = 0; i < poles.size(); i++) {
            auto* circle = new Part::GeomCircle();
            circle->setCenter(poles[i]);
            circle->setRadius(weights[i]);
            GeometryFacade::setConstruction(circle, true);
            circles.push_back(circle);
        }
        int firstCircle = getHighestCurveIndex() + 1;
        Obj->addGeometry(std::move(circles));

        // Same weight constraints as exposeInternalGeometry().
        std::stringstream stream;
        stream << "conList = []\n";
        for (size_t i = 0; i < poles.size(); i++) {
            int circleId = firstCircle + static_cast<int>(i);
            stream << "conList.append(Sketcher.Constraint('InternalAlignment:Sketcher::"
                      "BSplineControlPoint',"
                   << circleId << "," << static_cast<int>(PointPos::mid) << "," << geoId << ","
                   << i << "))\n";
            if (weights[0] != 1.0) {
                continue;
            }
            if (i == 0) {
                stream << "conList.append(Sketcher.Constraint('Weight'," << circleId << ",1.0))\n";
            }
            else if (weights[i] == weights[0]) {
                stream << "conList.append(Sketcher.Constraint('Equal'," << firstCircle << ","
                       << circleId << "))\n";
            }
        }
        stream << Gui::Command::getObjectCmd(sketchgui->getObject()) << ".addConstraint(conList)\n";
        stream << "del conList\n";
        Gui::Command::doCommand(Gui::Command::Doc, stream.str().c_str());

        // Knot points, which exposeInternalGeometry() places exactly on the curve.
        Gui::cmdAppObjectArgs(sketchgui->getObject(), "exposeInternalGeometry(%d)", geoId);
    }

    void jointOffsetCurves(std::vector<int>& listOfOffsetGeoIds)
    {
        if (listOfOffsetGeoIds.empty()) {
            return;
        }
        std::stringstream stream;
        stream << "conList = []\n";
        for (size_t i = 0; i < listOfOffsetGeoIds.size() - 1; i++) {
            for (size_t j = i + 1; j < listOfOffsetGeoIds.size(); j++) {
                // There's a bug with created arcs. They end points seems to swap at some point...
                // Here we make coincidences based on distance. So they must change after.
                Base::Vector3d firstStartPoint, firstEndPoint, secondStartPoint, secondEndPoint;
                if (!getFirstSecondPoints(listOfOffsetGeoIds[i], firstStartPoint, firstEndPoint)
                    || !getFirstSecondPoints(listOfOffsetGeoIds[j], secondStartPoint, secondEndPoint)) {
                    continue;
                }

                // Two curves can meet at both of their ends (e.g. two pieces closing a loop), but
                // each end point is joined only once. A second tangency between the same two
                // curves would be redundant (two arcs of one circle), so it is a coincidence.
                bool tangentUsed = false;
                const std::pair<PointPos, Base::Vector3d> firstEnds[2] = {
                    {PointPos::start, firstStartPoint},
                    {PointPos::end, firstEndPoint}
                };
                const std::pair<PointPos, Base::Vector3d> secondEnds[2] = {
                    {PointPos::start, secondStartPoint},
                    {PointPos::end, secondEndPoint}
                };
                bool firstUsed[2] = {false, false};
                bool secondUsed[2] = {false, false};
                for (int a = 0; a < 2; a++) {
                    for (int b = 0; b < 2; b++) {
                        if (firstUsed[a] || secondUsed[b]
                            || (firstEnds[a].second - secondEnds[b].second).Length()
                                >= Precision::Confusion()) {
                            continue;
                        }
                        firstUsed[a] = true;
                        secondUsed[b] = true;
                        PointPos posi = firstEnds[a].first;
                        PointPos posj = secondEnds[b].first;
                        bool tangent = !tangentUsed
                            && needTangent(listOfOffsetGeoIds[i], listOfOffsetGeoIds[j], posi, posj);
                        tangentUsed = tangentUsed || tangent;
                        stream << "conList.append(Sketcher.Constraint('"
                               << (tangent ? "Tangent" : "Coincident");
                        stream << "'," << listOfOffsetGeoIds[i] << "," << static_cast<int>(posi)
                               << ", " << listOfOffsetGeoIds[j] << "," << static_cast<int>(posj)
                               << "))\n";
                    }
                }
            }
        }

        // A non periodic B-spline whose ends meet (an offset cut at a single cusp) is closed.
        for (int geoId : listOfOffsetGeoIds) {
            const Part::Geometry* geo = sketchgui->getSketchObject()->getGeometry(geoId);
            Base::Vector3d startPoint, endPoint;
            if (isBSplineCurve(*geo) && !static_cast<const Part::GeomBSplineCurve*>(geo)->isPeriodic()
                && getFirstSecondPoints(geoId, startPoint, endPoint)
                && (startPoint - endPoint).Length() < Precision::Confusion()) {
                stream << "conList.append(Sketcher.Constraint('Coincident'," << geoId << ","
                       << static_cast<int>(PointPos::start) << ", " << geoId << ","
                       << static_cast<int>(PointPos::end) << "))\n";
            }
        }

        stream << Gui::Command::getObjectCmd(sketchgui->getObject()) << ".addConstraint(conList)\n";
        stream << "del conList\n";
        Gui::Command::doCommand(Gui::Command::Doc, stream.str().c_str());
    }

    bool needTangent(int geoId1, int geoId2, PointPos pos1, PointPos pos2)
    {
        // Todo: add cases for arcOfellipse parabolas hyperbolas bspline

        SketchObject* Obj = sketchgui->getSketchObject();
        const Part::Geometry* geo1 = Obj->getGeometry(geoId1);
        const Part::Geometry* geo2 = Obj->getGeometry(geoId2);

        if (!isArcOfCircle(*geo1) && !isArcOfCircle(*geo2)) {
            return false;
        }

        Base::Vector3d perpendicular1, perpendicular2, p1, p2;
        if (isArcOfCircle(*geo1)) {
            auto* arcOfCircle = static_cast<const Part::GeomArcOfCircle*>(geo1);
            p1 = pos1 == PointPos::start ? arcOfCircle->getStartPoint(true)
                                         : arcOfCircle->getEndPoint(true);

            perpendicular1.x = -(arcOfCircle->getCenter() - p1).y;
            perpendicular1.y = (arcOfCircle->getCenter() - p1).x;
        }
        else if (isLineSegment(*geo1)) {
            auto* line = static_cast<const Part::GeomLineSegment*>(geo1);
            perpendicular1 = line->getStartPoint() - line->getEndPoint();
        }
        else {
            return false;
        }

        if (isArcOfCircle(*geo2)) {
            auto* arcOfCircle = static_cast<const Part::GeomArcOfCircle*>(geo2);
            p2 = pos2 == PointPos::start ? arcOfCircle->getStartPoint(true)
                                         : arcOfCircle->getEndPoint(true);

            perpendicular2.x = -(arcOfCircle->getCenter() - p2).y;
            perpendicular2.y = (arcOfCircle->getCenter() - p2).x;
        }
        else if (isLineSegment(*geo2)) {
            auto* line = static_cast<const Part::GeomLineSegment*>(geo2);
            perpendicular2 = line->getStartPoint() - line->getEndPoint();
        }
        else {
            return false;
        }

        // if lines are parallel
        if ((perpendicular1 % perpendicular2).Length() < Precision::Confusion()) {
            return true;
        }

        return false;
    }

    void deleteOriginalGeometries()
    {
        std::stringstream stream;
        for (size_t j = 0; j < listOfGeoIds.size() - 1; j++) {
            stream << listOfGeoIds[j] << ",";
        }
        stream << listOfGeoIds[listOfGeoIds.size() - 1];
        try {
            Gui::cmdAppObjectArgs(sketchgui->getObject(), "delGeometries([%s])", stream.str().c_str());
        }
        catch (const Base::Exception& e) {
            Base::Console().error("{}\n", e.what());
        }
    }

    void makeOffsetConstraint(std::vector<int>& listOfOffsetGeoIds)
    {
        SketchObject* Obj = sketchgui->getSketchObject();

        std::stringstream stream;
        stream << "conList = []\n";
        // We separate the constraints of new lines in case the construction lines are not needed.
        std::stringstream newLinesStream;
        newLinesStream << "conList2 = []\n";

        vCCO = generatevCC(listOfOffsetGeoIds);

        // Stay undefined if no curve gets a construction line (e.g. offsets of B-splines).
        int geoIdCandidate1 = GeoEnum::GeoUndef;
        int geoIdCandidate2 = GeoEnum::GeoUndef;

        int newCurveCounter = 0;
        int prevCurveCounter = 0;
        std::vector<Part::Geometry*> geometriesToAdd;
        for (auto& curve : vCCO) {
            // Check if curve is closed. Note as we use pipe it should always be closed but in case
            // we enable 'Skin' in the future.
            bool closed = isCurveClosed(curve);
            bool atLeastOneLine = false;
            bool rerunFirstAfterThis = false;
            bool rerunningFirst = false;
            bool inTangentGroup = false;

            for (size_t j = 0; j < curve.size(); j++) {

                // Tangent constraint is constraining the offset already. So if there are tangents
                // we should not create the construction lines. Hence the code below.
                bool createLine = true;
                bool forceCreate = false;
                if (!inTangentGroup && (!closed || j != 0 || rerunningFirst)) {
                    createLine = true;
                    atLeastOneLine = true;
                }
                else {
                    // include case of j == 0 and closed curve, because if required the line
                    // will be made after last.
                    createLine = false;
                }

                if (j + 1 < curve.size()) {
                    inTangentGroup = areTangentCoincident(curve[j], curve[j + 1]);
                }
                else if (j == curve.size() - 1 && closed) {
                    // Case of last geoId for closed curves.
                    inTangentGroup = areTangentCoincident(curve[j], curve[0]);
                    if (inTangentGroup) {
                        if (!atLeastOneLine) {  // We need at least one line
                            createLine = true;
                            forceCreate = true;
                        }
                    }
                    else {
                        // We rerun the for at j=0 after this run to create line for j = 0.
                        rerunFirstAfterThis = true;
                    }
                }

                const Part::Geometry* geo = Obj->getGeometry(curve[j]);
                for (auto geoId : listOfGeoIds) {
                    // Check if geoId is the offsetted curve giving curve[j].
                    const Part::Geometry* geo2 = Obj->getGeometry(geoId);

                    if (isCircle(*geo) && isCircle(*geo2)) {
                        auto* circle = static_cast<const Part::GeomCircle*>(geo);
                        auto* circle2 = static_cast<const Part::GeomCircle*>(geo2);
                        Base::Vector3d p1 = circle->getCenter();
                        Base::Vector3d p2 = circle2->getCenter();
                        if ((p1 - p2).Length() < Precision::Confusion()) {
                            // coincidence of center
                            stream << "conList.append(Sketcher.Constraint('Coincident'," << curve[j]
                                   << ",3, " << geoId << ",3))\n";

                            // Create line between both circles.
                            auto* line = new Part::GeomLineSegment();
                            p1.x = p1.x + circle->getRadius();
                            p2.x = p2.x + circle2->getRadius();
                            line->setPoints(p1, p2);
                            GeometryFacade::setConstruction(line, true);
                            geometriesToAdd.push_back(line);
                            newCurveCounter++;
                            newLinesStream << "conList2.append(Sketcher.Constraint('Perpendicular',"
                                           << getHighestCurveIndex() + newCurveCounter << ", "
                                           << curve[j] << "))\n";
                            newLinesStream << "conList2.append(Sketcher.Constraint('PointOnObject',"
                                           << getHighestCurveIndex() + newCurveCounter << ",1, "
                                           << curve[j] << "))\n";
                            newLinesStream << "conList2.append(Sketcher.Constraint('PointOnObject',"
                                           << getHighestCurveIndex() + newCurveCounter << ",2, "
                                           << geoId << "))\n";

                            geoIdCandidate1 = curve[j];
                            geoIdCandidate2 = geoId;

                            break;
                        }
                    }
                    else if (isEllipse(*geo) && isEllipse(*geo2)) {
                        // same as circle but 2 lines
                    }
                    else if (isLineSegment(*geo) && isLineSegment(*geo2)) {
                        auto* lineSeg1 = static_cast<const Part::GeomLineSegment*>(geo);
                        auto* lineSeg2 = static_cast<const Part::GeomLineSegment*>(geo2);
                        Base::Vector3d p1[2], p2[2];
                        p1[0] = lineSeg1->getStartPoint();
                        p1[1] = lineSeg1->getEndPoint();
                        p2[0] = lineSeg2->getStartPoint();
                        p2[1] = lineSeg2->getEndPoint();
                        // if lines are parallel
                        if (((p1[1] - p1[0]) % (p2[1] - p2[0])).Length() < Precision::Intersection()) {
                            // If the lines are space by offsetLength distance
                            Base::Vector3d projectedP;
                            projectedP.ProjectToLine(p1[0] - p2[0], p2[1] - p2[0]);

                            if ((projectedP).Length() - fabs(offsetLength) < Precision::Confusion()) {
                                if (!forceCreate && !rerunningFirst) {
                                    stream << "conList.append(Sketcher.Constraint('Parallel',"
                                           << curve[j] << ", " << geoId << "))\n";
                                }

                                // We don't need a construction line if the line has a tangent at
                                // one end. Unless it's the first line that we're making.
                                if (createLine) {
                                    auto* line = new Part::GeomLineSegment();
                                    line->setPoints(p1[0], p1[0] + projectedP);
                                    GeometryFacade::setConstruction(line, true);
                                    geometriesToAdd.push_back(line);
                                    newCurveCounter++;

                                    newLinesStream
                                        << "conList2.append(Sketcher.Constraint('Perpendicular',"
                                        << getHighestCurveIndex() + newCurveCounter << ", "
                                        << curve[j] << "))\n";
                                    newLinesStream
                                        << "conList2.append(Sketcher.Constraint('Coincident',"
                                        << getHighestCurveIndex() + newCurveCounter << ",1, "
                                        << curve[j] << ",1))\n";
                                    newLinesStream
                                        << "conList2.append(Sketcher.Constraint('PointOnObject',"
                                        << getHighestCurveIndex() + newCurveCounter << ",2, "
                                        << geoId << "))\n";

                                    geoIdCandidate1 = curve[j];
                                    geoIdCandidate2 = geoId;
                                }
                                break;
                            }
                        }
                    }
                    else if (isArcOfCircle(*geo)) {
                        // multiple cases because arc join mode creates arcs or circle.
                        auto* arcOfCircle = static_cast<const Part::GeomArcOfCircle*>(geo);
                        Base::Vector3d p1 = arcOfCircle->getCenter();

                        if (isArcOfCircle(*geo2)) {
                            auto* arcOfCircle2 = static_cast<const Part::GeomArcOfCircle*>(geo2);
                            Base::Vector3d p2 = arcOfCircle2->getCenter();
                            Base::Vector3d p3 = arcOfCircle2->getStartPoint(true);
                            Base::Vector3d p4 = arcOfCircle2->getEndPoint(true);

                            if ((p1 - p2).Length() < Precision::Confusion()) {
                                // coincidence of center. Offset arc is the offset of an arc
                                stream << "conList.append(Sketcher.Constraint('Coincident',"
                                       << curve[j] << ",3, " << geoId << ",3))\n";
                                if (createLine) {
                                    // Create line between both circles.
                                    auto* line = new Part::GeomLineSegment();
                                    p1.x = p1.x + arcOfCircle->getRadius();
                                    p2.x = p2.x + arcOfCircle2->getRadius();
                                    line->setPoints(p1, p2);
                                    GeometryFacade::setConstruction(line, true);
                                    geometriesToAdd.push_back(line);
                                    newCurveCounter++;
                                    newLinesStream
                                        << "conList2.append(Sketcher.Constraint('Perpendicular',"
                                        << getHighestCurveIndex() + newCurveCounter << ", "
                                        << curve[j] << "))\n";
                                    newLinesStream
                                        << "conList2.append(Sketcher.Constraint('PointOnObject',"
                                        << getHighestCurveIndex() + newCurveCounter << ",1, "
                                        << curve[j] << "))\n";
                                    newLinesStream
                                        << "conList2.append(Sketcher.Constraint('PointOnObject',"
                                        << getHighestCurveIndex() + newCurveCounter << ",2, "
                                        << geoId << "))\n";

                                    geoIdCandidate1 = curve[j];
                                    geoIdCandidate2 = geoId;
                                }
                                break;
                            }
                            else if ((p1 - p3).Length() < Precision::Confusion()) {
                                // coincidence of center to startpoint. offset arc is created arc
                                // join
                                stream << "conList.append(Sketcher.Constraint('Coincident',"
                                       << curve[j] << ",3, " << geoId << ", 1))\n";

                                if (forceCreate) {
                                    stream << "conList.append(Sketcher.Constraint('Radius',"
                                           << curve[j] << ", " << offsetLength << "))\n";
                                }
                                break;
                            }
                            else if ((p1 - p4).Length() < Precision::Confusion()) {
                                // coincidence of center to startpoint
                                stream << "conList.append(Sketcher.Constraint('Coincident',"
                                       << curve[j] << ",3, " << geoId << ", 2))\n";

                                if (forceCreate) {
                                    stream << "conList.append(Sketcher.Constraint('Radius',"
                                           << curve[j] << ", " << offsetLength << "))\n";
                                }
                                break;
                            }
                        }
                        else if (
                            isLineSegment(*geo2) || isBSplineCurve(*geo2)
                            || geo2->is<Part::GeomArcOfConic>()
                        ) {
                            // cases where arc is created by arc join mode.
                            Base::Vector3d p2, p3;

                            if (getFirstSecondPoints(geoId, p2, p3)) {
                                bool startCoincidence = (p1 - p2).Length() < Precision::Confusion();
                                bool endCoincidence = (p1 - p3).Length() < Precision::Confusion();

                                if (startCoincidence || endCoincidence) {
                                    // coincidence of center to startpoint
                                    stream << "conList.append(Sketcher.Constraint('Coincident',"
                                           << curve[j] << ", 3, " << geoId << ", "
                                           << (startCoincidence ? 1 : 2) << "))\n";

                                    geoIdCandidate1 = curve[j];
                                    geoIdCandidate2 = geoId;

                                    break;
                                }
                            }
                        }
                    }
                    else if (isArcOfEllipse(*geo) && isArcOfEllipse(*geo2)) {
                        // const Part::GeomArcOfEllipse* arcOfEllipse = static_cast<const
                        // Part::GeomArcOfEllipse*>(geo2);
                    }
                    else if (isArcOfHyperbola(*geo) && isArcOfHyperbola(*geo2)) {
                        // const Part::GeomArcOfHyperbola* arcOfHyperbola = static_cast<const
                        // Part::GeomArcOfHyperbola*>(geo2);
                    }
                    else if (isArcOfParabola(*geo) && isArcOfParabola(*geo2)) {
                        // const Part::GeomArcOfParabola* arcOfParabola = static_cast<const
                        // Part::GeomArcOfParabola*>(geo2);
                    }
                    else if (isBSplineCurve(*geo) && isBSplineCurve(*geo2)) {
                    }
                }
                if (newCurveCounter != prevCurveCounter) {
                    prevCurveCounter = newCurveCounter;
                    if (newCurveCounter != 1) {
                        stream << "conList.append(Sketcher.Constraint('Equal',"
                               << getHighestCurveIndex() + newCurveCounter << ", "
                               << getHighestCurveIndex() + 1 << "))\n";
                    }
                }


                if (rerunningFirst) {
                    break;
                }

                if (rerunFirstAfterThis) {
                    j = -1;  // j will be incremented to 0 after new loop
                    rerunningFirst = true;
                }
            }
        }

        if (newCurveCounter >= 2) {
            stream << "conList.append(Sketcher.Constraint('Distance'," << getHighestCurveIndex() + 1
                   << ", " << fabs(offsetLength) << "))\n";

            Obj->addGeometry(std::move(geometriesToAdd));

            newLinesStream << Gui::Command::getObjectCmd(sketchgui->getObject())
                           << ".addConstraint(conList2)\n";
            newLinesStream << "del conList2\n";
            Gui::Command::doCommand(Gui::Command::Doc, newLinesStream.str().c_str());
        }
        else if (geoIdCandidate1 != GeoEnum::GeoUndef) {
            // If there is a single construction line, then its not needed.
            const Part::Geometry* geo = Obj->getGeometry(geoIdCandidate1);

            if (isCircle(*geo)) {
                stream << "conList.append(Sketcher.Constraint('Distance'," << geoIdCandidate1
                       << ", " << geoIdCandidate2 << ", " << fabs(offsetLength) << "))\n";
            }
            else if (isLineSegment(*geo)) {
                stream << "conList.append(Sketcher.Constraint('Distance'," << geoIdCandidate1
                       << ", 1," << geoIdCandidate2 << ", " << fabs(offsetLength) << "))\n";
            }
            else if (isArcOfCircle(*geo)) {
                const Part::Geometry* geo2 = Obj->getGeometry(geoIdCandidate2);
                if (isArcOfCircle(*geo2)) {
                    stream << "conList.append(Sketcher.Constraint('Distance'," << geoIdCandidate1
                           << ", 1," << geoIdCandidate2 << ", 1, " << fabs(offsetLength) << "))\n";
                }
                else if (isLineSegment(*geo2) || isBSplineCurve(*geo2)) {
                    stream << "conList.append(Sketcher.Constraint('Distance'," << geoIdCandidate1
                           << ", 3," << geoIdCandidate1 << ", 1, " << fabs(offsetLength) << "))\n";
                }
            }
        }

        stream << Gui::Command::getObjectCmd(sketchgui->getObject()) << ".addConstraint(conList)\n";
        stream << "del conList\n";
        Gui::Command::doCommand(Gui::Command::Doc, stream.str().c_str());
    }

    std::vector<std::vector<int>> generatevCC(std::vector<int>& listOfGeo)
    {
        // This function separates all the selected geometries into separate continuous curves.
        SketchObject* Obj = sketchgui->getSketchObject();
        std::vector<std::vector<int>> vcc;

        for (auto geoId : listOfGeo) {
            std::vector<int> vecOfGeoIds;
            const Part::Geometry* geo = Obj->getGeometry(geoId);
            if (isCircle(*geo) || isEllipse(*geo)
                || (isBSplineCurve(*geo)
                    && static_cast<const Part::GeomBSplineCurve*>(geo)->isPeriodic())) {
                vecOfGeoIds.push_back(geoId);
                vcc.push_back(vecOfGeoIds);
                continue;
            }

            bool inserted = false;
            int insertedIn = -1;
            for (size_t j = 0; j < vcc.size(); j++) {
                for (size_t k = 0; k < vcc[j].size(); k++) {
                    if (!areCoincident(geoId, vcc[j][k])) {
                        continue;
                    }

                    if (inserted && insertedIn != int(j)) {
                        // if it's already inserted in another continuous curve then we need
                        // to merge both curves together. There're 2 cases, it could have
                        // been inserted at the end or at the beginning.
                        if (vcc[insertedIn][0] == geoId) {
                            // Two cases. Either the coincident is at the beginning or at
                            // the end.
                            if (k == 0) {
                                std::reverse(vcc[j].begin(), vcc[j].end());
                            }
                            vcc[j].insert(vcc[j].end(), vcc[insertedIn].begin(), vcc[insertedIn].end());
                            vcc.erase(vcc.begin() + insertedIn);
                        }
                        else {
                            if (k != 0) {  // ie k is  vcc[j].size()-1
                                std::reverse(vcc[j].begin(), vcc[j].end());
                            }
                            vcc[insertedIn].insert(vcc[insertedIn].end(), vcc[j].begin(), vcc[j].end());
                            vcc.erase(vcc.begin() + j);
                        }
                        j--;
                    }
                    else {
                        // we need to get the curves in the correct order.
                        if (k == vcc[j].size() - 1) {
                            vcc[j].push_back(geoId);
                        }
                        else {
                            // in this case k should actually be 0.
                            vcc[j].insert(vcc[j].begin() + k, geoId);
                        }
                        insertedIn = j;
                        inserted = true;
                    }
                    // printCCeVec();
                    break;
                }
            }
            if (!inserted) {
                vecOfGeoIds.push_back(geoId);
                vcc.push_back(vecOfGeoIds);
            }
        }
        return vcc;
    }

    void generateSourceWires()
    {
        vCC = generatevCC(listOfGeoIds);

        SketchObject* Obj = sketchgui->getSketchObject();

        for (auto& CC : vCC) {
            BRepBuilderAPI_MakeWire mkWire;
            for (auto& curveId : CC) {
                const Part::Geometry* pGeo = Obj->getGeometry(curveId);
                auto geoCopy = std::unique_ptr<Part::Geometry>(pGeo->copy());
                Part::Geometry* geo = geoCopy.get();
                geo->reverseIfReversed();  // make sure we don't have reversed conics

                // Use the normalized copy to create the edge for the wire
                mkWire.Add(TopoDS::Edge(geo->toShape()));
            }

            TopoDS_Wire wire = mkWire.Wire();

            // Fix orientation: ensure all closed wires are CCW relative to Sketch Plane (+Z)
            if (wire.Closed()) {
                BRepBuilderAPI_MakeFace mkFace(wire);
                if (mkFace.IsDone()) {
                    TopoDS_Face face = mkFace.Face();
                    BRepAdaptor_Surface surf(face);
                    if (surf.GetType() == GeomAbs_Plane) {
                        gp_Dir norm = surf.Plane().Axis().Direction();
                        if (norm.Z() < 0) {
                            wire.Reverse();
                        }
                    }
                }
            }

            // Here we make sure that if possible the first wire is not a single line.
            if (CC.size() == 1 && isLineSegment(*Obj->getGeometry(CC[0]))) {
                sourceWires.push_back(wire);
            }
            else {
                sourceWires.insert(sourceWires.begin(), wire);
                onlySingleLines = false;
            }
        }
    }

    void findOffsetLength()
    {
        double newOffsetLength = std::numeric_limits<double>::max();

        BRepBuilderAPI_MakeVertex mkVertex({endpoint.x, endpoint.y, 0.0});
        TopoDS_Vertex vertex = mkVertex.Vertex();
        for (auto& wire : sourceWires) {
            BRepExtrema_DistShapeShape distTool(wire, vertex);
            if (distTool.IsDone()) {
                double distance = distTool.Value();
                if (distance == std::min(distance, newOffsetLength)) {
                    newOffsetLength = distance;

                    gp_Pnt pnt = distTool.PointOnShape1(1);
                    pointOnSourceWire = Base::Vector2d(pnt.X(), pnt.Y());

                    // find direction
                    if (BRep_Tool::IsClosed(wire)) {
                        TopoDS_Face aFace = BRepBuilderAPI_MakeFace(wire);
                        BRepClass_FaceClassifier checkPoint(
                            aFace,
                            {endpoint.x, endpoint.y, 0.0},
                            Precision::Confusion()
                        );
                        if (checkPoint.State() == TopAbs_IN) {
                            newOffsetLength = -newOffsetLength;
                        }
                    }
                }
            }
        }

        if (newOffsetLength != std::numeric_limits<double>::max()) {
            offsetLength = newOffsetLength;
        }
    }

    bool getFirstSecondPoints(int geoId, Base::Vector3d& startPoint, Base::Vector3d& endPoint)
    {
        const Part::Geometry* geo = sketchgui->getSketchObject()->getGeometry(geoId);

        if (isLineSegment(*geo)) {
            const auto* line = static_cast<const Part::GeomLineSegment*>(geo);
            startPoint = line->getStartPoint();
            endPoint = line->getEndPoint();
            return true;
        }
        else if (
            isArcOfCircle(*geo) || isArcOfEllipse(*geo) || isArcOfHyperbola(*geo)
            || isArcOfParabola(*geo)
        ) {
            const auto* arcOfConic = static_cast<const Part::GeomArcOfConic*>(geo);
            startPoint = arcOfConic->getStartPoint(true);
            endPoint = arcOfConic->getEndPoint(true);
            return true;
        }
        else if (isBSplineCurve(*geo)) {
            const auto* bSpline = static_cast<const Part::GeomBSplineCurve*>(geo);
            startPoint = bSpline->getStartPoint();
            endPoint = bSpline->getEndPoint();
            return true;
        }
        return false;
    }

    CoincidencePointPos checkForCoincidence(int geoId1, int geoId2, bool tangentOnly = false)
    {
        // This function looks up for 2 coincidence between 2 edges (arc + line can have 2)
        SketchObject* Obj = sketchgui->getSketchObject();
        const std::vector<Constraint*>& vals = Obj->Constraints.getValues();
        CoincidencePointPos positions;
        positions.firstPos1 = PointPos::none;
        positions.secondPos1 = PointPos::none;
        positions.firstPos2 = PointPos::none;
        positions.secondPos2 = PointPos::none;
        bool firstCoincidenceFound = false;
        for (auto* cstr : vals) {
            if (((tangentOnly || cstr->Type != Coincident) && cstr->Type != Tangent)
                || cstr->FirstPos == PointPos::mid || cstr->FirstPos == PointPos::none
                || cstr->SecondPos == PointPos::mid || cstr->SecondPos == PointPos::none) {
                continue;
            }

            if ((cstr->First == geoId1 && cstr->Second == geoId2)
                || (cstr->First == geoId2 && cstr->Second == geoId1)) {
                if (!firstCoincidenceFound) {
                    positions.firstPos1 = cstr->First == geoId1 ? cstr->FirstPos : cstr->SecondPos;
                    positions.secondPos1 = cstr->First == geoId2 ? cstr->FirstPos : cstr->SecondPos;
                    firstCoincidenceFound = true;
                }
                else {
                    positions.firstPos2 = cstr->First == geoId1 ? cstr->FirstPos : cstr->SecondPos;
                    positions.secondPos2 = cstr->First == geoId2 ? cstr->FirstPos : cstr->SecondPos;
                    break;
                }
            }
        }
        return positions;
    }

    bool areCoincident(int geoId1, int geoId2)
    {
        // Instead of checking for constraints like so:
        // CoincidencePointPos ppc = checkForCoincidence(geoId1, geoId2);
        // return ppc.firstPos1 != PointPos::none;
        // we are going to check if the points are effectively coincident:

        Base::Vector3d p11, p12, p21, p22;
        if (!getFirstSecondPoints(geoId1, p11, p12) || !getFirstSecondPoints(geoId2, p21, p22)) {
            return false;
        }

        return (
            (p11 - p21).Length() < Precision::Confusion()
            || (p11 - p22).Length() < Precision::Confusion()
            || (p12 - p21).Length() < Precision::Confusion()
            || (p12 - p22).Length() < Precision::Confusion()
        );
    }

    bool areTangentCoincident(int geoId1, int geoId2)
    {
        CoincidencePointPos ppc = checkForCoincidence(geoId1, geoId2, true);
        return ppc.firstPos1 != PointPos::none;
    }

    bool isCurveClosed(std::vector<int>& curve)
    {
        bool closed = false;
        if (curve.size() > 2) {
            closed = areCoincident(curve[0], curve[curve.size() - 1]);
        }
        else if (curve.size() == 2) {
            // if only 2 elements, we need to check if they close end to end.
            CoincidencePointPos cpp = checkForCoincidence(curve[0], curve[curve.size() - 1]);
            closed = cpp.firstPos1 != PointPos::none && cpp.firstPos2 != PointPos::none;
        }
        return closed;
    }

    // debug only
    /*void printCCeVec()
    {
        for (size_t j = 0; j < vCC.size(); j++) {
            Base::Console().warning("curve {}{", j);
            for (size_t k = 0; k < vCC[j].size(); k++) {
                Base::Console().warning("{}, ", vCC[j][k]);
            }
            Base::Console().warning("}\n");
        }
    }*/
};

template<>
auto DSHOffsetControllerBase::getState(int labelindex) const
{
    switch (labelindex) {
        case OnViewParameter::First:
            return SelectMode::SeekFirst;
            break;
        default:
            THROWM(Base::ValueError, "Parameter index without an associated machine state")
    }
}

template<>
void DSHOffsetController::configureToolWidget()
{
    if (!init) {  // Code to be executed only upon initialisation
        QStringList names = {
            QApplication::translate("Sketcher_CreateOffset", "Arc"),
            QApplication::translate("Sketcher_CreateOffset", "Intersection")
        };
        toolWidget->setComboboxElements(WCombobox::FirstCombo, names);

        toolWidget->setComboboxItemIcon(
            WCombobox::FirstCombo,
            0,
            Gui::BitmapFactory().iconFromTheme("Sketcher_OffsetArc")
        );
        toolWidget->setComboboxItemIcon(
            WCombobox::FirstCombo,
            1,
            Gui::BitmapFactory().iconFromTheme("Sketcher_OffsetIntersection")
        );

        toolWidget->setCheckboxLabel(
            WCheckbox::FirstBox,
            QApplication::translate("TaskSketcherTool_c1_offset", "Delete original geometries (U)")
        );
        toolWidget->setCheckboxLabel(
            WCheckbox::SecondBox,
            QApplication::translate("TaskSketcherTool_c2_offset", "Add offset constraint (J)")
        );
        toolWidget->setCheckboxToolTip(
            WCheckbox::FirstBox,
            QApplication::translate(
                "TaskSketcherTool_c1_offset",
                "Deletes the original geometry. If creating a single copy, this effectively "
                "performs a 'Move' operation."
            )
        );
        toolWidget->setCheckboxToolTip(
            WCheckbox::SecondBox,
            QApplication::translate(
                "TaskSketcherTool_c2_offset",
                "Adds a distance constraint with additional construction geometries that allows "
                "the distance to modify the entire offset geometry"

            )
        );
    }

    onViewParameters[OnViewParameter::First]->setLabelType(
        Gui::SoDatumLabel::DISTANCE,
        Gui::EditableDatumLabel::Function::Forced
    );
}

template<>
void DSHOffsetControllerBase::adaptDrawingToOnViewParameterChange(int labelindex, double value)
{
    switch (labelindex) {
        case OnViewParameter::First: {
            if (value == 0. && onViewParameters[OnViewParameter::First]->hasFinishedEditing) {
                // Do not accept 0, but only if user has finished editing the OVP.
                unsetOnViewParameter(onViewParameters[OnViewParameter::First].get());

                // reset offsetLengthSet so mouse can control the offset again
                handler->offsetLengthSet = false;

                Gui::NotifyUserError(
                    handler->sketchgui->getSketchObject(),
                    QT_TRANSLATE_NOOP("Notifications", "Invalid Value"),
                    QT_TRANSLATE_NOOP("Notifications", "Offset value can't be 0.")
                );
            }
            else {
                handler->offsetLengthSet = true;
                handler->offsetLength = value;
            }
        } break;
        default:
            break;
    }
}

template<>
void DSHOffsetController::adaptDrawingToCheckboxChange(int checkboxindex, bool value)
{
    switch (checkboxindex) {
        case WCheckbox::FirstBox:
            handler->deleteOriginal = value;

            // Both options cannot be enabled at the same time.
            if (value && toolWidget->getCheckboxChecked(WCheckbox::SecondBox)) {
                toolWidget->setCheckboxChecked(WCheckbox::SecondBox, false);
            }
            break;

        case WCheckbox::SecondBox:
            handler->offsetConstraint = value;

            // Both options cannot be enabled at the same time.
            if (value && toolWidget->getCheckboxChecked(WCheckbox::FirstBox)) {
                toolWidget->setCheckboxChecked(WCheckbox::FirstBox, false);
            }
            break;
    }
}

/* doEnforceControlParameters : The tool validates after offset length is set. So we don't need to
 * enforce it. Besides it is hard to override onsketchpos such that it is at offsetLength from the
 * curve. As we do not override the pos, we need to use offsetLengthSet to prevent rewrite of
 * offsetLength.*/

template<>
void DSHOffsetController::adaptParameters(Base::Vector2d onSketchPos)
{
    Q_UNUSED(onSketchPos)

    switch (handler->state()) {
        case SelectMode::SeekFirst: {
            auto& firstParam = onViewParameters[OnViewParameter::First];

            if (!firstParam->isSet) {
                setOnViewParameterValue(OnViewParameter::First, handler->offsetLength);
            }

            Base::Vector3d dimensionEndpoint;
            if (handler->offsetLengthSet && firstParam->isSet) {
                // if user has typed a value, calculate correct endpoint based on typed value
                Base::Vector2d direction = handler->endpoint - handler->pointOnSourceWire;
                if (direction.Length() > Precision::Confusion()) {
                    direction.Normalize();
                    Base::Vector2d correctedEndpoint = handler->pointOnSourceWire
                        + direction * handler->offsetLength;
                    dimensionEndpoint = Base::Vector3d(correctedEndpoint.x, correctedEndpoint.y, 0.);
                }
                else {
                    dimensionEndpoint = Base::Vector3d(handler->endpoint.x, handler->endpoint.y, 0.);
                }
            }
            else {
                // use mouse pos when user hasn't typed a value
                dimensionEndpoint = Base::Vector3d(handler->endpoint.x, handler->endpoint.y, 0.);
            }

            firstParam->setPoints(
                dimensionEndpoint,
                Base::Vector3d(handler->pointOnSourceWire.x, handler->pointOnSourceWire.y, 0.)
            );
        } break;
        default:
            break;
    }
}

template<>
void DSHOffsetController::computeNextDrawSketchHandlerMode()
{
    switch (handler->state()) {
        case SelectMode::SeekFirst: {
            auto& firstParam = onViewParameters[OnViewParameter::First];

            if (firstParam->hasFinishedEditing) {
                handler->setNextState(SelectMode::End);
            }
        } break;
        default:
            break;
    }
}


}  // namespace SketcherGui

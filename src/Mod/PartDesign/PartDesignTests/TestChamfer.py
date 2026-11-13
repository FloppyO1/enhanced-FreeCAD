# SPDX-License-Identifier: LGPL-2.1-or-later

# ***************************************************************************
# *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************

import math
import unittest

import FreeCAD
import TestSketcherApp


class TestChamfer(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestChamfer")

    def testChamferCubeToOctahedron(self):
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        self.Body.addObject(self.Box)
        self.Box.Length = 10.00
        self.Box.Width = 10.00
        self.Box.Height = 10.00
        self.Doc.recompute()
        self.Chamfer = self.Doc.addObject("PartDesign::Chamfer", "Chamfer")
        self.Chamfer.Base = (self.Box, ["Face" + str(i + 1) for i in range(6)])
        self.Chamfer.Size = 4.999999
        self.Body.addObject(self.Chamfer)
        self.Doc.recompute()
        self.MajorFaces = [face for face in self.Chamfer.Shape.Faces if face.Area > 1e-3]
        self.assertEqual(len(self.MajorFaces), 8)
        # test UseAllEdges property
        self.Chamfer.UseAllEdges = True
        self.Chamfer.Base = (self.Box, [""])  # no subobjects, should still work
        self.Doc.recompute()
        self.MajorFaces = [face for face in self.Chamfer.Shape.Faces if face.Area > 1e-3]
        self.assertEqual(len(self.MajorFaces), 8)
        self.Chamfer.Base = (self.Box, ["Face50"])  # non-existent face, test topo naming resilience
        self.Doc.recompute()
        self.MajorFaces = [face for face in self.Chamfer.Shape.Faces if face.Area > 1e-3]
        self.assertEqual(len(self.MajorFaces), 8)
        self.Chamfer.UseAllEdges = False
        self.Chamfer.Base = (self.Box, ["Face1"])
        self.Doc.recompute()
        self.MajorFaces = [face for face in self.Chamfer.Shape.Faces if face.Area > 1e-3]
        self.assertEqual(len(self.MajorFaces), 9)

    def testInsertPadBeforeChamferPreservesBaseThroughEdit(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = 10
        box.Width = 10
        box.Height = 10
        self.Doc.recompute()
        chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (box, ["Edge1"])
        chamfer.Size = 1
        self.Doc.recompute()
        self.assertTrue(chamfer.isValid())
        original_volume = chamfer.Shape.Volume

        body.Tip = box
        sketch = body.newObject("Sketcher::SketchObject", "InsertedSketch")
        sketch.Placement.Base = FreeCAD.Vector(0, 0, 2)
        TestSketcherApp.CreateRectangleSketch(sketch, (10, 3), (2, 4))
        pad = body.newObject("PartDesign::Pad", "InsertedPad")
        pad.Profile = sketch
        pad.Length = 4

        self.assertTrue(pad.Shape.isNull())
        self.assertEqual(chamfer.BaseFeature.Name, pad.Name)
        self.assertEqual(chamfer.Base[0].Name, pad.Name)
        self.Doc.recompute()
        self.assertTrue(pad.isValid())
        self.assertTrue(chamfer.isValid())

        chamfer.Base = (chamfer.Base[0], list(chamfer.Base[1]))
        body.Tip = chamfer
        self.Doc.recompute()
        self.assertEqual(chamfer.BaseFeature.Name, pad.Name)
        self.assertTrue(chamfer.isValid())
        self.assertAlmostEqual(body.Shape.BoundBox.XMax, 12)
        self.assertGreater(body.Shape.Volume, original_volume)

    def _edge_name(self, shape, p1, p2):
        p1, p2 = FreeCAD.Vector(*p1), FreeCAD.Vector(*p2)
        for index, edge in enumerate(shape.Edges):
            ends = [v.Point for v in edge.Vertexes]
            if len(ends) == 2 and (
                (ends[0].isEqual(p1, 1e-6) and ends[1].isEqual(p2, 1e-6))
                or (ends[0].isEqual(p2, 1e-6) and ends[1].isEqual(p1, 1e-6))
            ):
                return "Edge" + str(index + 1)
        self.fail("Edge not found")

    def _assert_valid_single_solid(self, feature):
        self.assertTrue(feature.isValid())
        self.assertTrue(feature.Shape.isValid())
        self.assertEqual(len(feature.Shape.Solids), 1)

    def testChamferLargerThanPocketWall(self):
        # box with a narrow pocket: the walls are 2 wide, the chamfer size is 3
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 20, 10, 10
        pocket = body.newObject("PartDesign::SubtractiveBox", "Pocket")
        pocket.Length, pocket.Width, pocket.Height = 16, 6, 5
        pocket.Placement.Base = FreeCAD.Vector(2, 2, 5)
        self.Doc.recompute()
        chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (pocket, [self._edge_name(pocket.Shape, (0, 0, 10), (20, 0, 10))])
        chamfer.Size = 3
        self.Doc.recompute()
        self._assert_valid_single_solid(chamfer)
        # triangle 3x3 at the ends, trapezoid (3 + 1) / 2 * 2 on the wall
        expected = 20 * 10 * 10 - 16 * 6 * 5 - (4 * 4.5 + 16 * 4)
        self.assertLess(abs(chamfer.Shape.Volume - expected) / expected, 1e-3)
        # the preview shows the new faces even though the boolean fallback was used
        chamfer.invalidatePreview()
        chamfer.updatePreview()
        preview = chamfer.PreviewShape
        self.assertFalse(preview.isNull())
        self.assertGreater(len(preview.Faces), 0)

    def testChamferThroughHoles(self):
        # plate with through holes near the edge, the chamfer crosses the holes
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 40, 20, 5
        last = box
        for x in (10, 20, 30):
            last = body.newObject("PartDesign::SubtractiveCylinder", "Hole")
            last.Radius, last.Height = 1, 5
            last.Placement.Base = FreeCAD.Vector(x, 2, 0)
        self.Doc.recompute()
        chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (last, [self._edge_name(last.Shape, (0, 0, 5), (40, 0, 5))])
        chamfer.Size = 4
        self.Doc.recompute()
        self._assert_valid_single_solid(chamfer)
        # the hole (area pi, centered at y = 2) loses a height of 4 - 2 on average
        expected = 40 * 20 * 5 - 3 * math.pi * 5 - 40 * 8 + 3 * math.pi * 2
        self.assertLess(abs(chamfer.Shape.Volume - expected) / expected, 1e-3)

    def testChamferNormalCaseUsesOcc(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 10, 10, 10
        self.Doc.recompute()
        names = [
            self._edge_name(box.Shape, (10, 10, 10), end)
            for end in ((0, 10, 10), (10, 0, 10), (10, 10, 0))
        ]
        chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (box, names)
        chamfer.Size = 2
        self.Doc.recompute()
        self._assert_valid_single_solid(chamfer)
        occ = box.Shape.makeChamfer(2, [box.Shape.getElement(n) for n in names])
        self.assertAlmostEqual(chamfer.Shape.Volume, occ.Volume, places=6)
        self.assertEqual(len(chamfer.Shape.Faces), len(occ.Faces))

    def _make_step(self, body):
        # 10 x 10 x 20 block, the step removes x > 4, z > 10: the wall side is 10 high, the
        # step top is 6 wide
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 10, 10, 20
        step = body.newObject("PartDesign::SubtractiveBox", "Step")
        step.Length, step.Width, step.Height = 6, 10, 10
        step.Placement.Base = FreeCAD.Vector(4, 0, 10)
        self.Doc.recompute()
        return step, self._edge_name(step.Shape, (4, 0, 10), (4, 10, 10))

    def _make_boss(self, body):
        # 30 x 30 x 5 plate with a 4 x 20 x 10 boss on it
        plate = body.newObject("PartDesign::AdditiveBox", "Plate")
        plate.Length, plate.Width, plate.Height = 30, 30, 5
        boss = body.newObject("PartDesign::AdditiveBox", "Boss")
        boss.Length, boss.Width, boss.Height = 4, 20, 10
        boss.Placement.Base = FreeCAD.Vector(13, 5, 5)
        self.Doc.recompute()
        return boss, self._edge_name(boss.Shape, (13, 5, 5), (13, 5, 15))

    def _assert_occ_fails(self, make):
        try:
            result = make()
        except Exception:
            return
        self.assertFalse(result.isValid() and len(result.Solids) == 1, "OCC did not fail")

    def _assert_inside_envelope(self, shape, base):
        bb, bbBase = shape.BoundBox, base.BoundBox
        for attr in ("XMin", "YMin", "ZMin"):
            self.assertGreaterEqual(getattr(bb, attr), getattr(bbBase, attr) - 1e-6)
        for attr in ("XMax", "YMax", "ZMax"):
            self.assertLessEqual(getattr(bb, attr), getattr(bbBase, attr) + 1e-6)

    def _assert_plate_untouched(self, shape):
        import Part

        plate = Part.makeBox(30, 30, 5)
        self.assertAlmostEqual(shape.common(plate).Volume, plate.Volume, places=4)
        self.assertAlmostEqual(shape.BoundBox.ZMin, 0)

    def testChamferConcaveClippedToSolid(self):
        # the fused chamfer (12) is cut flat by the outer side (x = 10) and the wall top (z = 20)
        body = self.Doc.addObject("PartDesign::Body", "Body")
        step, edge = self._make_step(body)
        self._assert_occ_fails(lambda: step.Shape.makeChamfer(12, [step.Shape.getElement(edge)]))
        chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (step, [edge])
        chamfer.Size = 12
        self.Doc.recompute()
        self._assert_valid_single_solid(chamfer)
        self._assert_inside_envelope(chamfer.Shape, step.Shape)
        # below z = 22 - (x - 4) inside x 4..10, z 10..20: 2 * 10 + (6 + 2) / 2 * 4
        expected = step.Shape.Volume + 10 * 52
        self.assertLess(abs(chamfer.Shape.Volume - expected) / expected, 1e-3)

    def testChamferConvexStopsAtFloor(self):
        # vertical boss edge, size 6 larger than the 4 wide boss face: the cut stops at the floor
        body = self.Doc.addObject("PartDesign::Body", "Body")
        boss, edge = self._make_boss(body)
        self._assert_occ_fails(lambda: boss.Shape.makeChamfer(6, [boss.Shape.getElement(edge)]))
        chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (boss, [edge])
        chamfer.Size = 6
        self.Doc.recompute()
        self._assert_valid_single_solid(chamfer)
        self._assert_plate_untouched(chamfer.Shape)
        # trapezoid (6 + 2) / 2 * 4 removed over the boss height
        expected = boss.Shape.Volume - 10 * 16
        self.assertLess(abs(chamfer.Shape.Volume - expected) / expected, 1e-3)

    def _make_curved_step(self, body):
        # 20 x 10 x 5 base with an upper part on x 0..10 whose top is a cylinder (axis along y
        # through x = 5, z = 5, radius 5 * sqrt(2)): the 5 high wall at x = 10 meets the
        # cylinder at a convex junction at z = 10, the top reaches z = 5 + 5 * sqrt(2)
        base = body.newObject("PartDesign::AdditiveBox", "Base")
        base.Length, base.Width, base.Height = 20, 10, 5
        roof = body.newObject("PartDesign::AdditiveCylinder", "Roof")
        roof.Radius, roof.Height = 5 * 2**0.5, 10
        roof.Placement = FreeCAD.Placement(
            FreeCAD.Vector(5, 0, 5), FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), -90)
        )
        last = roof
        for name, size, position in (
            ("Left", (10, 20, 30), (-10, -5, -10)),
            ("Right", (20, 20, 20), (10, -5, 5)),
            ("Below", (40, 20, 10), (-10, -5, -10)),
        ):
            last = body.newObject("PartDesign::SubtractiveBox", name)
            last.Length, last.Width, last.Height = size
            last.Placement.Base = FreeCAD.Vector(*position)
        self.Doc.recompute()
        self.assertEqual(len(last.Shape.Solids), 1)
        return last, self._edge_name(last.Shape, (10, 0, 5), (10, 10, 5))

    def testChamferLimitedByCurvedFace(self):
        # size 8 would put the chamfer above the 5 high wall, past the cylindrical top: the
        # size is reduced so that it stays on the wall (5 with 1% margin)
        body = self.Doc.addObject("PartDesign::Body", "Body")
        step, edge = self._make_curved_step(body)
        self._assert_occ_fails(lambda: step.Shape.makeChamfer(8, [step.Shape.getElement(edge)]))
        chamfer = body.newObject("PartDesign::Chamfer", "Chamfer")
        chamfer.Base = (step, [edge])
        chamfer.Size = 8
        self.Doc.recompute()
        self._assert_valid_single_solid(chamfer)
        self.assertAlmostEqual(chamfer.Size.Value, 8)
        self._assert_inside_envelope(chamfer.Shape, step.Shape)
        added = chamfer.Shape.Volume - step.Shape.Volume
        self.assertLessEqual(added, 10 * 5**2 / 2)
        expected = 10 * 4.95**2 / 2
        self.assertLess(abs(added - expected) / expected, 1e-3)
        import Part

        extra = chamfer.Shape.cut(step.Shape)
        corner = Part.makeBox(5, 10, 5, FreeCAD.Vector(10, 0, 5))
        self.assertAlmostEqual(extra.common(corner).Volume, extra.Volume, places=4)

    def _make_tangent_step(self, body):
        # 20 x 10 x 5 base with an upper part on x 0..10: a 3 high wall at x = 10 continues
        # tangentially at z = 8 into a cylindrical top (axis along y through x = 5, z = 8,
        # radius 5) that curves away from the corner
        base = body.newObject("PartDesign::AdditiveBox", "Base")
        base.Length, base.Width, base.Height = 20, 10, 5
        wall = body.newObject("PartDesign::AdditiveBox", "Wall")
        wall.Length, wall.Width, wall.Height = 10, 10, 3
        wall.Placement.Base = FreeCAD.Vector(0, 0, 5)
        roof = body.newObject("PartDesign::AdditiveCylinder", "Roof")
        roof.Radius, roof.Height = 5, 10
        roof.Placement = FreeCAD.Placement(
            FreeCAD.Vector(5, 0, 8), FreeCAD.Rotation(FreeCAD.Vector(1, 0, 0), -90)
        )
        below = body.newObject("PartDesign::SubtractiveBox", "Below")
        below.Length, below.Width, below.Height = 40, 20, 10
        below.Placement.Base = FreeCAD.Vector(-10, -5, -10)
        self.Doc.recompute()
        self.assertEqual(len(below.Shape.Solids), 1)
        return below, self._edge_name(below.Shape, (10, 0, 5), (10, 10, 5))

    def testChamferLimitedByTangentCurvedFace(self):
        # size 12 would put the chamfer past the 3 high wall and past the tangent cylindrical
        # top, into the air beside it: the size is reduced to the wall (3 with 1% margin)
        body = self.Doc.addObject("PartDesign::Body", "Body")
        step, edge = self._make_tangent_step(body)
        self._assert_occ_fails(lambda: step.Shape.makeChamfer(12, [step.Shape.getElement(edge)]))
        feature = body.newObject("PartDesign::Chamfer", "Chamfer")
        feature.Base = (step, [edge])
        feature.Size = 12
        self.Doc.recompute()
        self._assert_valid_single_solid(feature)
        self.assertAlmostEqual(feature.Size.Value, 12)
        self._assert_inside_envelope(feature.Shape, step.Shape)
        added = feature.Shape.Volume - step.Shape.Volume
        expected = 10 * 2.97**2 * 0.5
        self.assertLess(abs(added - expected) / expected, 1e-3)
        # the added material is all in the corner x 10..13, z 5..8, beside the planar wall
        import Part

        extra = feature.Shape.cut(step.Shape)
        corner = Part.makeBox(3, 10, 3, FreeCAD.Vector(10, 0, 5))
        self.assertAlmostEqual(extra.common(corner).Volume, extra.Volume, places=4)

    def _make_groove(self, body):
        # 30 x 10 x 20 block with a groove x 10..14, z 14..20 across it: the dressed-up edge is
        # the top left edge (x = 0, z = 20), its top face is 10 wide and its side face 20 high
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 30, 10, 20
        groove = body.newObject("PartDesign::SubtractiveBox", "Groove")
        groove.Length, groove.Width, groove.Height = 4, 12, 6
        groove.Placement.Base = FreeCAD.Vector(10, -1, 14)
        self.Doc.recompute()
        self.assertAlmostEqual(groove.Shape.Volume, 30 * 10 * 20 - 4 * 10 * 6)
        return groove, self._edge_name(groove.Shape, (0, 0, 20), (0, 10, 20))

    def _vertex_name(self, shape, point):
        point = FreeCAD.Vector(*point)
        for index, vertex in enumerate(shape.Vertexes):
            if vertex.Point.isEqual(point, 1e-6):
                return "Vertex" + str(index + 1)
        self.fail("Vertex not found")

    def _face_name(self, shape, center):
        center = FreeCAD.Vector(*center)
        for index, face in enumerate(shape.Faces):
            if face.CenterOfMass.isEqual(center, 1e-6):
                return "Face" + str(index + 1)
        self.fail("Face not found")

    def _up_to(self, kind, references, size):
        # dressed-up groove edge in UpTo mode, size ignored (it is computed)
        body = self.Doc.addObject("PartDesign::Body", "Body")
        groove, edge = self._make_groove(body)
        feature = body.newObject("PartDesign::" + kind, kind)
        feature.Base = (groove, [edge])
        setattr(feature, size, 1)
        feature.SizeMode = "UpTo"
        feature.UpToReferences = [(groove, ref(groove.Shape)) for ref in references]
        self.Doc.recompute()
        self._assert_valid_single_solid(feature)
        return groove, feature

    def _check_chamfer_up_to(self, references, size):
        groove, chamfer = self._up_to("Chamfer", references, "Size")
        self.assertAlmostEqual(chamfer.Size.Value, size, places=6)
        self.assertTrue(chamfer.getPropertyStatus("Size").count("ReadOnly"))
        return groove, chamfer

    def testChamferUpToVertex(self):
        groove, chamfer = self._check_chamfer_up_to(
            [lambda s: self._vertex_name(s, (10, 0, 14))], 6
        )
        self.assertAlmostEqual(chamfer.Shape.Volume, groove.Shape.Volume - 10 * 18, places=6)

    def testChamferUpToEdge(self):
        groove, chamfer = self._check_chamfer_up_to(
            [lambda s: self._edge_name(s, (14, 0, 14), (14, 10, 14))], 6
        )
        self.assertAlmostEqual(chamfer.Shape.Volume, groove.Shape.Volume - 10 * 18, places=6)

    def testChamferUpToNearestFace(self):
        groove, chamfer = self._check_chamfer_up_to(
            [
                lambda s: self._face_name(s, (14, 5, 17)),
                lambda s: self._face_name(s, (12, 5, 14)),
            ],
            6,
        )
        self.assertAlmostEqual(chamfer.Shape.Volume, groove.Shape.Volume - 10 * 18, places=6)

    def testChamferUpToBeyondFace(self):
        # 14 along the 10 wide top face: the boolean fallback removes the part of the triangle
        # (0, 20), (14, 20), (0, 6) inside x 0..10, an area of 90
        groove, chamfer = self._check_chamfer_up_to(
            [lambda s: self._vertex_name(s, (14, 0, 20))], 14
        )
        self.assertAlmostEqual(chamfer.Shape.Volume, groove.Shape.Volume - 10 * 90, places=4)

    def _make_half_hole_step(self, body, height, z, seam, refine=None):
        # the 20 x 20 x 20 step block (x > 10, z > 10 removed) with a radius 3 hole whose
        # vertical axis (10, 10) lies on the wall of the step: the hole is open on the side of
        # the upper part. The cylinder is turned by seam degrees around its axis.
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 20, 20, 20
        step = body.newObject("PartDesign::SubtractiveBox", "Step")
        step.Length, step.Width, step.Height = 10, 20, 10
        step.Placement.Base = FreeCAD.Vector(10, 0, 10)
        hole = body.newObject("PartDesign::SubtractiveCylinder", "Hole")
        hole.Radius, hole.Height = 3, height
        hole.Placement = FreeCAD.Placement(
            FreeCAD.Vector(10, 10, z), FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), seam)
        )
        if refine is not None:
            for feature in (box, step, hole):
                feature.Refine = refine
        self.Doc.recompute()
        self.assertEqual(len(hole.Shape.Solids), 1)
        return hole

    def _circle_names(self, shape, radius, z, side=None):
        # circular edges of the given radius at height z, on the side x > 10 (side 1) or
        # x < 10 (side -1) of the step wall
        names = []
        for index, edge in enumerate(shape.Edges):
            curve = edge.Curve
            if (
                "Circle" in str(curve)
                and abs(curve.Radius - radius) < 1e-9
                and abs(curve.Center.z - z) < 1e-9
            ):
                mid = edge.valueAt((edge.FirstParameter + edge.LastParameter) / 2)
                if side is None or (mid.x - 10) * side > 0:
                    names.append("Edge" + str(index + 1))
        self.assertTrue(names)
        return names

    def _assert_occ_fails_on(self, base, names, make):
        self._assert_occ_fails(lambda: make(base.Shape, [base.Shape.getElement(n) for n in names]))

    def _ring(self, r, rho, angle):
        # Pappus: section of the chamfer of size r turned by angle, its centroid at rho from the
        # axis (for a straight edge, angle is the length and rho 1)
        return (r**2 / 2) * angle * rho

    def _offset(self, r):
        # distance of the centroid of the section from its corner, along each face
        return r / 3

    def _dress_up(self, base, names, r):
        body = base.getParentGeoFeatureGroup()
        feature = body.newObject("PartDesign::Chamfer", "Chamfer")
        feature.Base = (base, names)
        feature.Size = r
        self.Doc.recompute()
        self._assert_valid_single_solid(feature)
        return feature

    def testChamferHalfHoleOnStep(self):
        # through hole on the step wall, seam of the cylinder at the end of the arc that the
        # hole leaves on the step top (z = 10, x > 10): OCC fails even at size 1. The boolean
        # fallback revolves the section around the axis and cuts it.
        body = self.Doc.addObject("PartDesign::Body", "Body")
        hole = self._make_half_hole_step(body, 30, -5, 90)
        names = self._circle_names(hole.Shape, 3, 10, 1)
        self.assertEqual(len(names), 1)
        self._assert_occ_fails_on(hole, names, lambda s, e: s.makeChamfer(1, e))
        for r in (1.0, 3.0):
            feature = self._dress_up(hole, names, r)
            removed = hole.Shape.Volume - feature.Shape.Volume
            expected = self._ring(r, 3 + self._offset(r), math.pi)
            self.assertLess(abs(removed - expected) / expected, 1e-6)
            self._assert_inside_envelope(feature.Shape, hole.Shape)
            feature.invalidatePreview()
            feature.updatePreview()
            self.assertGreater(len(feature.PreviewShape.Faces), 0)

    def testChamferHalfHoleInLowerPart(self):
        # the hole only goes through the lower part: its rim on the step top is split in two
        # quarter arcs by the seam, the circle continues tangentially under the upper part
        body = self.Doc.addObject("PartDesign::Body", "Body")
        hole = self._make_half_hole_step(body, 10, 0, 0)
        names = self._circle_names(hole.Shape, 3, 10, 1)
        self.assertEqual(len(names), 2)
        self._assert_occ_fails_on(hole, names, lambda s, e: s.makeChamfer(3, e))
        feature = self._dress_up(hole, names, 3.0)
        removed = hole.Shape.Volume - feature.Shape.Volume
        expected = self._ring(3.0, 3 + self._offset(3.0), math.pi)
        self.assertLess(abs(removed - expected) / expected, 1e-6)
        # the material under the upper part (x < 10) is untouched
        import Part

        under = Part.makeBox(10, 20, 10)
        self.assertAlmostEqual(
            feature.Shape.common(under).Volume, hole.Shape.common(under).Volume, places=6
        )

    def testChamferHalfHoleFloor(self):
        # blind half hole in the upper part, without refine: OCC fails on the concave arc at
        # its floor. The fallback fuses the revolved section inside the hole, also with the
        # size equal to the radius of the hole (the section reaches the axis).
        body = self.Doc.addObject("PartDesign::Body", "Body")
        hole = self._make_half_hole_step(body, 10, 10, 0, refine=False)
        names = self._circle_names(hole.Shape, 3, 10, -1)
        self.assertEqual(len(names), 1)
        self._assert_occ_fails_on(hole, names, lambda s, e: s.makeChamfer(1, e))
        for r in (1.0, 3.0):
            feature = self._dress_up(hole, names, r)
            added = feature.Shape.Volume - hole.Shape.Volume
            expected = self._ring(r, 3 - self._offset(r), math.pi)
            self.assertLess(abs(added - expected) / expected, 1e-6)
            self._assert_inside_envelope(feature.Shape, hole.Shape)

    def testChamferArcCircleAndLine(self):
        # one feature on the half hole arc (OCC fails), the full rim of a small hole and a
        # straight edge of the step top: all made by the fallback
        body = self.Doc.addObject("PartDesign::Body", "Body")
        self._make_half_hole_step(body, 30, -5, 90)
        small = body.newObject("PartDesign::SubtractiveCylinder", "SmallHole")
        small.Radius, small.Height = 1, 10
        small.Placement.Base = FreeCAD.Vector(15, 16.5, 0)
        self.Doc.recompute()
        shape = small.Shape
        names = (
            self._circle_names(shape, 3, 10, 1)
            + self._circle_names(shape, 1, 10)
            + [self._edge_name(shape, (20, 0, 10), (20, 20, 10))]
        )
        r = 1.5
        feature = self._dress_up(small, names, r)
        removed = small.Shape.Volume - feature.Shape.Volume
        expected = (
            self._ring(r, 3 + self._offset(r), math.pi)
            + self._ring(r, 1 + self._offset(r), 2 * math.pi)
            + self._ring(r, 1, 20)
        )
        self.assertLess(abs(removed - expected) / expected, 1e-6)

    def testChamferUpToOnArc(self):
        # the vertex (10, 0, 10) is 7 from the rim of the half hole on the step top
        body = self.Doc.addObject("PartDesign::Body", "Body")
        hole = self._make_half_hole_step(body, 30, -5, 90)
        names = self._circle_names(hole.Shape, 3, 10, 1)
        feature = body.newObject("PartDesign::Chamfer", "Chamfer")
        feature.Base = (hole, names)
        feature.SizeMode = "UpTo"
        feature.UpToReferences = [(hole, self._vertex_name(hole.Shape, (10, 0, 10)))]
        self.Doc.recompute()
        self._assert_valid_single_solid(feature)
        self.assertAlmostEqual(feature.Size.Value, 7, places=6)
        removed = hole.Shape.Volume - feature.Shape.Volume
        expected = self._ring(7, 3 + self._offset(7), math.pi)
        self.assertLess(abs(removed - expected) / expected, 1e-6)

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("PartDesignTestChamfer")
        # print ("omit closing document for debugging")

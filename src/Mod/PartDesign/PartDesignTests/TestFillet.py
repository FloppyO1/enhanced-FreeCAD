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

from __future__ import division
from math import pi
import unittest

import FreeCAD
import TestSketcherApp


class TestFillet(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("PartDesignTestFillet")

    def _create_box_with_fillet(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        box.Length = 10.00
        box.Width = 10.00
        box.Height = 10.00
        body.addObject(box)
        self.Doc.recompute()

        fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (box, ["Edge1"])
        fillet.Radius = 1.0
        body.addObject(fillet)
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())
        return body, box, fillet

    def _find_edge_with_match_count(self, source_shape, target_shape, match_count):
        for index in range(1, source_shape.countElement("Edge") + 1):
            source_name = "Edge" + str(index)
            source_edge = source_shape.getElement(source_name, True)
            matches = target_shape.findSubShapesWithSharedVertex(
                source_edge,
                needName=True,
                checkGeometry=True,
            )
            if len(matches) == match_count:
                return source_name, matches[0][0] if matches else None
        self.skipTest("Test model did not contain a suitable edge")

    def testFilletCubeToSphere(self):
        self.Body = self.Doc.addObject("PartDesign::Body", "Body")
        self.Box = self.Doc.addObject("PartDesign::AdditiveBox", "Box")
        self.Body.addObject(self.Box)
        self.Box.Length = 10.00
        self.Box.Width = 10.00
        self.Box.Height = 10.00
        self.Doc.recompute()
        self.Fillet = self.Doc.addObject("PartDesign::Fillet", "Fillet")
        self.Fillet.Base = (self.Box, ["Face" + str(i + 1) for i in range(6)])
        self.Fillet.Radius = 4.999999
        self.Body.addObject(self.Fillet)
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4 / 3 * pi * 5**3, places=3)
        # test UseAllEdges property
        self.Fillet.UseAllEdges = True
        self.Fillet.Base = (self.Box, [""])  # no subobjects, should still work
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4 / 3 * pi * 5**3, places=3)
        self.Fillet.Base = (self.Box, ["Face50"])  # non-existent face, topo naming resilience
        self.Doc.recompute()
        self.assertAlmostEqual(self.Fillet.Shape.Volume, 4 / 3 * pi * 5**3, places=3)
        self.Fillet.UseAllEdges = False
        self.Fillet.Base = (self.Box, ["Face1"])
        self.Doc.recompute()
        self.assertNotAlmostEqual(self.Fillet.Shape.Volume, 4 / 3 * pi * 5**3, places=3)

    def testDeletingPreviousFeatureRelinksUniqueMatchingBaseEdge(self):
        body, box, fillet = self._create_box_with_fillet()
        old_edge, new_edge = self._find_edge_with_match_count(fillet.Shape, box.Shape, 1)

        followup = self.Doc.addObject("PartDesign::Fillet", "FollowupFillet")
        followup.Base = (fillet, [old_edge])
        followup.Radius = 0.25
        body.addObject(followup)
        self.Doc.recompute()
        self.assertTrue(followup.isValid())

        body.removeObject(fillet)

        self.assertEqual(followup.Base[0].Name, box.Name)
        self.assertEqual(list(followup.Base[1]), [new_edge])

    def testDeletingPreviousFeatureLeavesUnmatchedBaseEdgeUnresolved(self):
        body, box, fillet = self._create_box_with_fillet()
        old_edge, _new_edge = self._find_edge_with_match_count(fillet.Shape, box.Shape, 0)

        followup = self.Doc.addObject("PartDesign::Fillet", "FollowupFillet")
        followup.Base = (fillet, [old_edge])
        followup.Radius = 0.25
        body.addObject(followup)
        self.Doc.recompute()

        body.removeObject(fillet)

        self.assertEqual(followup.BaseFeature.Name, box.Name)
        self.assertEqual(followup.Base[0].Name, box.Name)
        self.assertEqual(len(followup.Base[1]), 1)
        self.assertTrue(all(ref.startswith("?") for ref in followup.Base[1]))
        self.Doc.recompute()
        self.assertFalse(followup.isValid())

    def testMovePadAfterFilletLeavesMissingEdgeWithoutDependencyCycle(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = 10
        box.Width = 10
        box.Height = 10
        self.Doc.recompute()

        top_face = max(
            range(1, len(box.Shape.Faces) + 1),
            key=lambda index: box.Shape.Faces[index - 1].CenterOfMass.z,
        )
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        sketch.AttachmentSupport = (box, [f"Face{top_face}"])
        sketch.MapMode = "FlatFace"
        TestSketcherApp.CreateRectangleSketch(sketch, (2, 2), (2, 2))
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sketch
        pad.Length = 4
        self.Doc.recompute()
        side_edge = next(
            index
            for index, edge in enumerate(pad.Shape.Edges, 1)
            if edge.BoundBox.ZMin >= 10 - 1e-7
            and edge.BoundBox.ZLength > 1
            and edge.BoundBox.XLength < 1e-7
            and edge.BoundBox.YLength < 1e-7
        )
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (pad, [f"Edge{side_edge}"])
        fillet.Radius = 0.25
        self.Doc.recompute()
        self.assertTrue(fillet.isValid())

        body.removeObject(pad)
        body.insertObject(pad, fillet, True)

        self.assertEqual(fillet.BaseFeature.Name, box.Name)
        self.assertEqual(fillet.Base[0].Name, box.Name)
        self.assertEqual(pad.BaseFeature.Name, fillet.Name)
        self.assertNotIn(pad, fillet.OutList)
        self.assertEqual(len(fillet.Base[1]), 1)
        self.assertTrue(all(ref.startswith("?") for ref in fillet.Base[1]))
        self.Doc.recompute(None, False, True)
        self.assertFalse(fillet.isValid())

    def testInsertPadBeforeFilletPreservesBaseThroughEditAndUndo(self):
        body, box, fillet = self._create_box_with_fillet()
        original_volume = fillet.Shape.Volume

        self.Doc.openTransaction("Insert pad before fillet")
        body.Tip = box
        sketch = body.newObject("Sketcher::SketchObject", "InsertedSketch")
        sketch.Placement.Base = FreeCAD.Vector(0, 0, 2)
        TestSketcherApp.CreateRectangleSketch(sketch, (10, 3), (2, 4))
        pad = body.newObject("PartDesign::Pad", "InsertedPad")
        pad.Profile = sketch
        pad.Length = 4

        self.assertTrue(pad.Shape.isNull())
        self.assertEqual(fillet.BaseFeature.Name, pad.Name)
        self.assertEqual(fillet.Base[0].Name, pad.Name)
        self.Doc.recompute()
        self.assertTrue(pad.isValid())
        self.assertTrue(fillet.isValid())

        fillet.Base = (fillet.Base[0], list(fillet.Base[1]))
        body.Tip = fillet
        self.Doc.recompute()
        self.assertEqual(fillet.BaseFeature.Name, pad.Name)
        self.assertTrue(fillet.isValid())
        self.assertAlmostEqual(body.Shape.BoundBox.XMax, 12)
        self.assertGreater(body.Shape.Volume, original_volume)
        self.Doc.commitTransaction()

        self.Doc.undo()
        self.Doc.recompute()
        self.assertEqual(fillet.BaseFeature.Name, box.Name)
        self.assertEqual(fillet.Base[0].Name, box.Name)
        self.assertAlmostEqual(body.Shape.Volume, original_volume)

        self.Doc.redo()
        self.Doc.recompute()
        restored_pad = self.Doc.getObject("InsertedPad")
        self.assertIsNotNone(restored_pad)
        self.assertEqual(fillet.BaseFeature.Name, restored_pad.Name)
        self.assertEqual(fillet.Base[0].Name, restored_pad.Name)
        self.assertTrue(fillet.isValid())
        self.assertAlmostEqual(body.Shape.BoundBox.XMax, 12)

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

    def _integrate(self, func, a, b, steps=2000):
        # Simpson rule
        h = (b - a) / steps
        total = func(a) + func(b)
        for i in range(1, steps):
            total += (4 if i % 2 else 2) * func(a + i * h)
        return total * h / 3

    def _add_cut_box(self, body, name, size, position):
        cut = body.newObject("PartDesign::SubtractiveBox", name)
        cut.Length, cut.Width, cut.Height = size
        cut.Placement.Base = FreeCAD.Vector(*position)
        return cut

    def _assert_valid_single_solid(self, feature):
        self.assertTrue(feature.isValid())
        self.assertTrue(feature.Shape.isValid())
        self.assertEqual(len(feature.Shape.Solids), 1)

    def testFilletRadiusLargerThanPocketWall(self):
        # box with a narrow pocket: the walls are 2 wide, the fillet radius is 3
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 20, 10, 10
        pocket = self._add_cut_box(body, "Pocket", (16, 6, 5), (2, 2, 5))
        self.Doc.recompute()
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (pocket, [self._edge_name(pocket.Shape, (0, 0, 10), (20, 0, 10))])
        radius = 3.0
        fillet.Radius = radius
        self.Doc.recompute()
        self._assert_valid_single_solid(fillet)
        # cross section removed at distance y from the outer face
        height = lambda y: radius - (radius**2 - (radius - y) ** 2) ** 0.5
        full = radius**2 * (1 - pi / 4)
        wall = self._integrate(height, 0, 2)
        expected = 20 * 10 * 10 - 16 * 6 * 5 - (4 * full + 16 * wall)
        self.assertLess(abs(fillet.Shape.Volume - expected) / expected, 1e-3)
        # the preview shows the new faces even though the boolean fallback was used
        fillet.invalidatePreview()
        fillet.updatePreview()
        preview = fillet.PreviewShape
        self.assertFalse(preview.isNull())
        self.assertGreater(len(preview.Faces), 0)

    def testFilletThroughHoles(self):
        # plate with through holes near the edge, the fillet crosses the holes
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 40, 20, 5
        last = box
        for x in (10, 20, 30):
            last = body.newObject("PartDesign::SubtractiveCylinder", "Hole")
            last.Radius, last.Height = 1, 5
            last.Placement.Base = FreeCAD.Vector(x, 2, 0)
        self.Doc.recompute()
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (last, [self._edge_name(last.Shape, (0, 0, 5), (40, 0, 5))])
        radius = 4.0
        fillet.Radius = radius
        self.Doc.recompute()
        self._assert_valid_single_solid(fillet)
        height = lambda y: radius - (radius**2 - (radius - y) ** 2) ** 0.5
        chord = lambda y: 2 * max(0.0, 1 - (y - 2) ** 2) ** 0.5
        inHoles = 3 * self._integrate(lambda y: chord(y) * height(y), 1, 3)
        expected = 40 * 20 * 5 - 3 * pi * 5 - 40 * radius**2 * (1 - pi / 4) + inHoles
        self.assertLess(abs(fillet.Shape.Volume - expected) / expected, 1e-3)

    def testFilletNormalCaseUsesOcc(self):
        # three edges at a corner: OCC makes a spherical corner, the fallback would not
        body = self.Doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length, box.Width, box.Height = 10, 10, 10
        self.Doc.recompute()
        names = [
            self._edge_name(box.Shape, (10, 10, 10), end)
            for end in ((0, 10, 10), (10, 0, 10), (10, 10, 0))
        ]
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (box, names)
        fillet.Radius = 2
        self.Doc.recompute()
        self._assert_valid_single_solid(fillet)
        occ = box.Shape.makeFillet(2, [box.Shape.getElement(n) for n in names])
        self.assertAlmostEqual(fillet.Shape.Volume, occ.Volume, places=6)
        self.assertTrue(any("Sphere" in str(f.Surface) for f in fillet.Shape.Faces))

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

    def testFilletConcaveClippedToSolid(self):
        # the fused fillet (R 12) is wider than the step top and higher than the wall side:
        # it is cut flat by the outer side (x = 10) and by the top of the wall (z = 20)
        body = self.Doc.addObject("PartDesign::Body", "Body")
        step, edge = self._make_step(body)
        radius = 12.0
        self._assert_occ_fails(lambda: step.Shape.makeFillet(radius, [step.Shape.getElement(edge)]))
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (step, [edge])
        fillet.Radius = radius
        self.Doc.recompute()
        self._assert_valid_single_solid(fillet)
        self._assert_inside_envelope(fillet.Shape, step.Shape)
        # arc centered at (16, 22), material added below it inside x 4..10, z 10..20
        arc = lambda x: min(20.0, 22 - (radius**2 - (16 - x) ** 2) ** 0.5) - 10
        expected = step.Shape.Volume + 10 * self._integrate(arc, 4, 10)
        self.assertLess(abs(fillet.Shape.Volume - expected) / expected, 1e-3)

    def testFilletConvexStopsAtFloor(self):
        # vertical boss edge, R 6 larger than the 4 wide boss face: the cut stops at the floor
        body = self.Doc.addObject("PartDesign::Body", "Body")
        boss, edge = self._make_boss(body)
        radius = 6.0
        self._assert_occ_fails(lambda: boss.Shape.makeFillet(radius, [boss.Shape.getElement(edge)]))
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (boss, [edge])
        fillet.Radius = radius
        self.Doc.recompute()
        self._assert_valid_single_solid(fillet)
        self._assert_plate_untouched(fillet.Shape)
        # arc centered at (19, 11), boss material removed below it inside x 13..17
        arc = lambda x: 6 - (radius**2 - (19 - x) ** 2) ** 0.5
        expected = boss.Shape.Volume - 10 * self._integrate(arc, 13, 17)
        self.assertLess(abs(fillet.Shape.Volume - expected) / expected, 1e-3)

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

    def testFilletLimitedByCurvedFace(self):
        # radius 8 would put the fillet above the 5 high wall, past the cylindrical top: the
        # radius is reduced so that the tangent line stays on the wall (5 with 1% margin)
        body = self.Doc.addObject("PartDesign::Body", "Body")
        step, edge = self._make_curved_step(body)
        self._assert_occ_fails(lambda: step.Shape.makeFillet(8, [step.Shape.getElement(edge)]))
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (step, [edge])
        fillet.Radius = 8
        self.Doc.recompute()
        self._assert_valid_single_solid(fillet)
        self.assertAlmostEqual(fillet.Radius.Value, 8)
        self._assert_inside_envelope(fillet.Shape, step.Shape)
        added = fillet.Shape.Volume - step.Shape.Volume
        self.assertLessEqual(added, 10 * 5**2 * (1 - pi / 4))
        expected = 10 * 4.95**2 * (1 - pi / 4)
        self.assertLess(abs(added - expected) / expected, 1e-3)
        # the added material is all in the corner x 10..15, z 5..10
        import Part

        extra = fillet.Shape.cut(step.Shape)
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

    def testFilletLimitedByTangentCurvedFace(self):
        # size 12 would put the fillet past the 3 high wall and past the tangent cylindrical
        # top, into the air beside it: the size is reduced to the wall (3 with 1% margin)
        body = self.Doc.addObject("PartDesign::Body", "Body")
        step, edge = self._make_tangent_step(body)
        self._assert_occ_fails(lambda: step.Shape.makeFillet(12, [step.Shape.getElement(edge)]))
        feature = body.newObject("PartDesign::Fillet", "Fillet")
        feature.Base = (step, [edge])
        feature.Radius = 12
        self.Doc.recompute()
        self._assert_valid_single_solid(feature)
        self.assertAlmostEqual(feature.Radius.Value, 12)
        self._assert_inside_envelope(feature.Shape, step.Shape)
        added = feature.Shape.Volume - step.Shape.Volume
        expected = 10 * 2.97**2 * (1 - pi / 4)
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

    def _check_fillet_up_to(self, references, radius):
        groove, fillet = self._up_to("Fillet", references, "Radius")
        self.assertAlmostEqual(fillet.Radius.Value, radius, places=6)
        self.assertTrue(fillet.getPropertyStatus("Radius").count("ReadOnly"))
        return groove, fillet

    def testFilletUpToVertex(self):
        # vertex at the bottom of the groove: 10 along the top face, 6 down the side face
        groove, fillet = self._check_fillet_up_to(
            [lambda s: self._vertex_name(s, (10, 0, 14))], 6
        )
        expected = groove.Shape.Volume - 10 * 36 * (1 - pi / 4)
        self.assertLess(abs(fillet.Shape.Volume - expected) / expected, 1e-6)

    def testFilletUpToEdge(self):
        groove, fillet = self._check_fillet_up_to(
            [lambda s: self._edge_name(s, (14, 0, 14), (14, 10, 14))], 6
        )
        expected = groove.Shape.Volume - 10 * 36 * (1 - pi / 4)
        self.assertLess(abs(fillet.Shape.Volume - expected) / expected, 1e-6)

    def testFilletUpToNearestFace(self):
        # the right wall of the groove is 14 away along the top face, its bottom 6 down the side
        groove, fillet = self._check_fillet_up_to(
            [
                lambda s: self._face_name(s, (14, 5, 17)),
                lambda s: self._face_name(s, (12, 5, 14)),
            ],
            6,
        )
        expected = groove.Shape.Volume - 10 * 36 * (1 - pi / 4)
        self.assertLess(abs(fillet.Shape.Volume - expected) / expected, 1e-6)

    def testFilletUpToBeyondFace(self):
        # the vertex is 14 away along the 10 wide top face: the boolean fallback makes the fillet
        groove, fillet = self._check_fillet_up_to(
            [lambda s: self._vertex_name(s, (14, 0, 20))], 14
        )
        # arc centered at (14, 6), material removed above it for x 0..10
        arc = lambda x: 20 - 6 - (14**2 - (14 - x) ** 2) ** 0.5
        expected = groove.Shape.Volume - 10 * self._integrate(arc, 0, 10)
        self.assertLess(abs(fillet.Shape.Volume - expected) / expected, 1e-4)

    def testFilletUpToWithoutReference(self):
        body = self.Doc.addObject("PartDesign::Body", "Body")
        groove, edge = self._make_groove(body)
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (groove, [edge])
        fillet.SizeMode = "UpTo"
        self.Doc.recompute()
        self.assertFalse(fillet.isValid())

    def tearDown(self):
        # closing doc
        FreeCAD.closeDocument("PartDesignTestFillet")
        # print ("omit closing document for debugging")

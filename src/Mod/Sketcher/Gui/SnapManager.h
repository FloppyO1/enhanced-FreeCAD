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
 *   SnapManager initially funded by the Open Toolchain Foundation         *
 ***************************************************************************/

#pragma once

#include <App/Application.h>

#include <Base/Tools2D.h>
#include <Base/Vector3D.h>

#include <Mod/Sketcher/App/GeoEnum.h>

class SbVec2s;

namespace Gui
{
class View3DInventorViewer;
}

namespace Part
{
class GeomLineSegment;
class GeomArcOfCircle;
}  // namespace Part

namespace SketcherGui
{

class ViewProviderSketch;


class ViewProviderSketchSnapAttorney
{
private:
    static inline int getPreselectPoint(const ViewProviderSketch& vp);
    static inline int getPreselectCross(const ViewProviderSketch& vp);
    static inline int getPreselectCurve(const ViewProviderSketch& vp);

    friend class SnapManager;
};

enum class SnapType
{
    None = 0x0,
    Angle = 0x1,
    Point = 0x2,
    Edge = 0x4,
    Grid = 0x8,

    All = Angle | Point | Edge | Grid
};

/** Element of the 3D model (outside of the sketch) the cursor has been snapped onto.
 *
 * The element is not part of the sketch yet. Importing ObjectName.SubName as external geometry
 * creates the sketch geometry that PosId refers to.
 */
struct ModelReference
{
    enum class Kind
    {
        None,
        Point,        ///< A point: a vertex, the center of a circular edge or a degenerate edge
        Edge,         ///< A point on an edge
        EdgeMidpoint  ///< The middle of a straight edge
    };

    Kind kind = Kind::None;
    std::string objectName;
    std::string subName;
    /// Position (sketch coordinates) the cursor is snapped to
    Base::Vector2d position;
    /// Point of the imported external geometry the position corresponds to (none for edges)
    Sketcher::PointPos posId = Sketcher::PointPos::none;

    bool isValid() const
    {
        return kind != Kind::None;
    }
};

/* This class is used to manage the overriding of mouse pointer coordinates in Sketcher
 *  (in Edit-Mode) depending on the situation. Those situations are in priority order :
 *  1 - Snap at angle: For tools like Slot, Arc, Line, Ellipse, this enables to constrain the angle
 * at steps of 5° (or customized angle). This is useful to make features at a certain angle (45° for
 * example) 2 - Snap to object: This snaps the mouse pointer onto objects. 3 - Snap to grid: This
 * snaps the mouse pointer on the grid.
 */
class SnapManager
{

    /** @brief      Class for monitoring changes in parameters affecting Snapping
     *  @details
     *
     * This nested class is a helper responsible for attaching to the parameters relevant for
     * SnapManager, initialising the SnapManager to the current configuration
     * and handle in real time any change to their values.
     */
    class ParameterObserver: public ParameterGrp::ObserverType
    {
    public:
        explicit ParameterObserver(SnapManager& client);
        ~ParameterObserver() override;

        void subscribeToParameters();

        void unsubscribeToParameters();

        /** Observer for parameter group. */
        void OnChange(Base::Subject<const char*>& rCaller, const char* sReason) override;

    private:
        void initParameters();
        void updateSnapParameter(const std::string& parametername);
        void updateSnapToObjectParameter(const std::string& parametername);
        void updateSnapToModelParameter(const std::string& parametername);
        void updateSnapToGridParameter(const std::string& parametername);
        void updateSnapAngleParameter(const std::string& parametername);

        static ParameterGrp::handle getParameterGrpHandle();

    private:
        std::map<std::string, std::function<void(const std::string&)>> str2updatefunction;
        SnapManager& client;
    };

public:
    explicit SnapManager(ViewProviderSketch& vp);
    ~SnapManager();

    Base::Vector2d snap(Base::Vector2d inputPos, SnapType mask);
    bool snapAtAngle(Base::Vector2d inputPos, Base::Vector2d& snapPos);
    bool snapToObject(Base::Vector2d inputPos, Base::Vector2d& snapPos, SnapType mask);
    bool snapToGrid(Base::Vector2d inputPos, Base::Vector2d& snapPos);
    /// Snaps to vertices, edges and circle centers of the 3D model while a tool is active
    bool snapToModel(Base::Vector2d inputPos, Base::Vector2d& snapPos);

    /** Looks for the vertex or edge of the 3D model (outside of the sketch) under the cursor.
     * To be called on mouse move, before snapping. The 3D view does not preselect other objects
     * while a sketch is edited, so the model is picked here.
     */
    void updateModelPick(const SbVec2s& cursorPos, const Gui::View3DInventorViewer* viewer);
    void clearModelPick();

    /** Returns the model element the last computed position was snapped to, or an invalid
     * reference if that position was not snapped to the model. */
    const ModelReference& getModelReference() const
    {
        return modelReference;
    }

    bool snapToLineMiddle(Base::Vector3d& pointToOverride, const Part::GeomLineSegment* line);
    bool snapToArcMiddle(Base::Vector3d& pointToOverride, const Part::GeomArcOfCircle* arc);

    void setAngleSnapping(bool enable, Base::Vector2d referencepoint);

    struct SnapHandle
    {
        SnapManager* mgr = nullptr;
        Base::Vector2d cursorPos;

        SnapHandle(SnapManager* m, const Base::Vector2d& cursorPos)
            : mgr(m)
            , cursorPos(cursorPos)
        {}

        Base::Vector2d compute(SnapType mask = SnapType::All);
    };

private:
    /// Reference to ViewProviderSketch in order to access the public and the Attorney Interface
    ViewProviderSketch& viewProvider;

    bool angleSnapRequested;
    bool snapRequested;
    bool snapToObjectsRequested;
    bool snapToModelRequested;
    bool snapToGridRequested;

    /// Model element under the cursor, see updateModelPick()
    struct ModelPick
    {
        std::string objectName;
        std::string element;      ///< VertexN or EdgeN
        Base::Vector3d point;     ///< Picked point, in global coordinates
    };
    ModelPick modelPick;

    /// Result of the last snapToModel()
    ModelReference modelReference;
    /// Center of the circular model edge hovered last, offered as snap target near the center
    ModelReference acquiredCenter;

    Base::Vector2d referencePoint;
    double lastMouseAngle;

    double snapAngle;

    /// Observer to track all the needed parameters.
    std::unique_ptr<SnapManager::ParameterObserver> pObserver;
};


}  // namespace SketcherGui

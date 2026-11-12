// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 The FreeCAD project association AISBL               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <FCGlobal.h>
#include <Inventor/SbVec2s.h>
#include <Inventor/SbVec3f.h>
#include "Inventor/SoNaviCube.h"

class QMouseEvent;
class QWidget;
class SoMaterial;
class SoGroup;
class SoSeparator;
class SoTransform;

namespace Gui
{
class View3DInventor;
class View3DInventorViewer;

/**
 * Large, almost transparent orientation cube drawn around the model of a 3D view.
 * While it is shown the view can be rotated as usual, but the model cannot be picked:
 * clicking a face, edge or corner of the cube orients the camera like the matching
 * navigation cube face, fits the model and hides the cube.
 */
class GuiExport ViewOrientationCube: public QObject
{
    Q_OBJECT

public:
    explicit ViewOrientationCube(View3DInventor* view);
    ~ViewOrientationCube() override;

    /// Returns the cube of \a view, creating it if \a create is true.
    static ViewOrientationCube* of(View3DInventor* view, bool create);
    /// Shows the cube of \a view, or hides it when it is already shown.
    static void toggle(View3DInventor* view);

    bool isShown() const
    {
        return shown;
    }
    bool show();
    void hide();

    /// Picks the cube under the viewport position \a pos (Coin coordinates).
    SoNaviCube::PickId pickAt(const SbVec2s& pos) const;
    /// Orients the camera to \a id, fits the model and hides the cube.
    void activate(SoNaviCube::PickId id);

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildSceneNodes();
    int pickRegion(const SbVec2s& pos) const;
    void setHilite(int region);
    bool isViewerWidget(QObject* obj) const;
    SbVec2s toCoin(const QPoint& pos) const;
    void fitWhenIdle(int retries);

    QPointer<View3DInventor> view;
    View3DInventorViewer* viewer {nullptr};
    SoSeparator* root {nullptr};
    SoMaterial* faceMaterial {nullptr};
    SoTransform* xform {nullptr};
    SoGroup* parent {nullptr};
    SbVec3f center;
    float half {1.0F};
    bool shown {false};
    int hilite {-1};

    // left button tracking: a click picks the cube, a drag is passed to the navigation
    bool leftDown {false};
    bool dragging {false};
    bool replaying {false};
    QPoint pressPos;
    QPointF pressGlobalPos;
    Qt::KeyboardModifiers pressModifiers;
};

}  // namespace Gui

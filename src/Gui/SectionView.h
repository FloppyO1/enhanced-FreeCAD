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

#include <string>
#include <vector>
#include <QColor>
#include <QObject>
#include <QPointer>
#include <FCGlobal.h>
#include <fastsignals/signal.h>
#include <Base/Vector3D.h>
#include "Selection/Selection.h"

class QDockWidget;
class QEvent;
class QKeyEvent;
class QVBoxLayout;
class QPushButton;
class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;
class QToolButton;
class QWidget;
class QDoubleSpinBox;
class QSpinBox;
class QGroupBox;
class SoAction;
class SoEvent;
class SoCallback;
class SoClipPlane;
class SoGLRenderAction;
class SoCoordinate3;
class SoEventCallback;
class SoGroup;
class SoIndexedFaceSet;
class SoMaterial;
class SoSeparator;
class SoSwitch;
class SoTextureCoordinate2;
class SoTransform;
class SoScale;

namespace Gui
{
class View3DInventor;
class QuantitySpinBox;
class ColorButton;

/**
 * Quick section view of a 3D view: a single clipping plane that can be toggled
 * on and off. While the section is being edited the plane follows the current
 * selection (faces, edges, vertices, origin or datum planes) and can be moved
 * with an arrow or a numeric offset. The cut area is shown as a hatched cap.
 * One instance lives as a child of each 3D view, so the last plane is kept
 * while the section is turned off.
 */
class GuiExport SectionView: public QObject, public SelectionObserver
{
    Q_OBJECT

public:
    enum class Source
    {
        View,
        Selection
    };

    explicit SectionView(View3DInventor* view);
    ~SectionView() override;

    /// Returns the section of \a view, creating it if \a create is true.
    static SectionView* of(View3DInventor* view, bool create);
    /// Adds the floating toggle button at the top center of \a view.
    static void installOverlayButton(View3DInventor* view);
    /// Toggles the section of \a view and keeps command and overlay states in sync.
    static void toggle(View3DInventor* view, bool on);

    bool isEnabled() const
    {
        return enabled;
    }
    void setEnabled(bool on);
    bool isConfirmed() const
    {
        return confirmed;
    }
    /// Locks the section: panel, arrow and plane square are hidden.
    void confirm();
    /// Shows the panel again to edit a confirmed section.
    void editAgain();

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct PlaneState
    {
        Source source {Source::View};
        Base::Vector3d basePoint;
        Base::Vector3d normal {0, 0, 1};
        double offset {0.0};
        bool axial {false};
        bool containsAxis {false};
        Base::Vector3d axisPoint;
        Base::Vector3d axisDir {0, 0, 1};
        Base::Vector3d axisRef {1, 0, 0};
        double angle {0.0};
        bool hasSecondRef {false};
    };

    void onSelectionChanged(const SelectionChanges& msg) override;

    struct RefItem
    {
        QString text;
        std::string doc;
        std::string obj;
        std::string sub;
    };

    void buildPanel();
    void buildSceneNodes();
    void setDefaultPlane();
    void updateFromSelection(bool resetIfEmpty);
    void rebuildReferenceList();
    void removeReference(const RefItem& item);
    void clearReferences();
    bool planeFromSelection();
    bool handleKey(QKeyEvent* event, bool shortcutOverride);
    bool editingBlocked() const;
    void applyState();
    void updateStepFromModel();
    void updateArrowSize();
    void updatePlane();
    void updateOffsetSpin();
    void updateAxisWidgets();
    void collectTriangles();
    void renderCap(SoGLRenderAction* action);
    static void capCB(void* data, SoAction* action);
    void updateToggleStates();
    void updateArrowVisibility();
    void loadAppearance();
    void saveAppearance();
    void appearanceChanged();
    void setArrowHighlight(bool on);
    Base::Vector3d planePoint() const;
    Base::Vector3d clipNormal() const;
    void onActivateView(const QWidget* activated);
    static void eventCB(void* data, SoEventCallback* cb);

private:
    QPointer<View3DInventor> view;
    bool enabled {false};
    bool reversed {false};
    PlaneState st;
    bool confirmed {false};
    std::vector<RefItem> refItems;

    // Cap appearance
    struct CapStyle
    {
        QColor fill {224, 219, 209};
        int transparency {0};  // percent
        int hatch {1};         // 0 none, 1 single lines, 2 cross-hatch
        double angle {45.0};   // degrees
        double spacing {8.0};  // pixels
        double lineWidth {1.0};
        QColor line {89, 56, 31};
    };
    CapStyle capStyle;

    // dragging
    bool dragging {false};
    bool highlighted {false};
    double dragStartOffset {0.0};
    double dragStartParam {0.0};

    // Scene graph
    SoGroup* root {nullptr};
    SoClipPlane* clip {nullptr};
    SoSeparator* overlay {nullptr};  // arrow, plane outline, cap, event callback
    SoSwitch* arrowSwitch {nullptr};
    SoSeparator* arrowRoot {nullptr};
    SoTransform* arrowXform {nullptr};
    SoScale* arrowScale {nullptr};
    SoMaterial* arrowMaterial {nullptr};
    SoCoordinate3* planeCoords {nullptr};
    SoCallback* capNode {nullptr};
    bool showCap {true};
    std::vector<float> capTris;  // visible faces in world coordinates
    double modelSize {10.0};
    double clipShift {0.0};  // tiny shift of the clip plane into the material

    // Panel
    QPointer<QDockWidget> dock;
    QCheckBox* reverseBox {nullptr};
    QWidget* refsContainer {nullptr};
    QVBoxLayout* refsLayout {nullptr};
    QLabel* refsHint {nullptr};
    QWidget* axisWidget {nullptr};
    QComboBox* axisMode {nullptr};
    QLabel* axisLabel {nullptr};
    QuantitySpinBox* angleSpin {nullptr};
    QuantitySpinBox* offsetSpin {nullptr};
    QCheckBox* arrowBox {nullptr};
    QCheckBox* capBox {nullptr};
    QLabel* infoLabel {nullptr};
    QGroupBox* appearanceBox {nullptr};
    ColorButton* fillButton {nullptr};
    QSpinBox* transparencySpin {nullptr};
    QComboBox* hatchCombo {nullptr};
    QDoubleSpinBox* hatchAngleSpin {nullptr};
    QDoubleSpinBox* spacingSpin {nullptr};
    QDoubleSpinBox* lineWidthSpin {nullptr};
    ColorButton* lineButton {nullptr};
    QTimer* capTimer {nullptr};

    fastsignals::scoped_connection activateViewConnection;
    fastsignals::scoped_connection recomputedConnection;
    fastsignals::scoped_connection changedObjectConnection;
};

}  // namespace Gui

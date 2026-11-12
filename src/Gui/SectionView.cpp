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

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <numbers>
#include <sstream>

#include <Inventor/SbLine.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/actions/SoCallbackAction.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/SoPrimitiveVertex.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/nodes/SoShape.h>
#include <Inventor/system/gl.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoCone.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoCylinder.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoRotation.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoTranslation.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ElementNamingUtils.h>
#include <App/Datums.h>
#include <App/GeoFeature.h>
#include <Base/Interpreter.h>
#include <Base/Tools.h>

#include "SectionView.h"
#include "Action.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "Control.h"
#include "Document.h"
#include "MainWindow.h"
#include "QuantitySpinBox.h"
#include "Widgets.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewProviderDocumentObject.h"
#include "Inventor/So3DAnnotation.h"

using namespace Gui;

namespace
{

constexpr const char* overlayButtonName = "SectionViewOverlayButton";

struct Reference
{
    enum class Kind
    {
        None,
        Point,
        Plane,
        Axis
    };
    Kind kind {Kind::None};
    Base::Vector3d point;   // point on plane / axis, or the point itself
    Base::Vector3d dir;     // plane normal or axis direction
    Base::Vector3d center;  // center of the referenced element
    bool free {false};      // plane without material side (origin planes)
};

// Geometry is evaluated with the Part module, which the core GUI cannot link to.
// Placements of containers are applied by Part.getShape() because the sub-name
// is resolved from the root object.
const char* sectionScript = R"(
import FreeCAD
import Part

def _vec(v):
    return (v.x, v.y, v.z)

def classify(obj, sub):
    try:
        s = Part.getShape(obj, sub, needSubElement=True)
    except Exception:
        return ('none',)
    if s.isNull():
        return ('none',)
    if s.ShapeType not in ('Vertex', 'Edge', 'Face'):
        if len(s.Faces) == 1:
            s = s.Faces[0]
        elif not s.Faces and len(s.Edges) == 1:
            s = s.Edges[0]
        else:
            c = _vec(s.BoundBox.Center)
            return ('point', c, (0, 0, 0), c)
    if s.ShapeType == 'Vertex':
        return ('point', _vec(s.Point), (0, 0, 0), _vec(s.Point))
    com = _vec(s.CenterOfMass)
    if s.ShapeType == 'Edge':
        c = s.Curve
        n = type(c).__name__
        if n in ('Circle', 'Ellipse', 'ArcOfCircle', 'ArcOfEllipse'):
            return ('axis', _vec(c.Center), _vec(c.Axis), _vec(c.Center))
        if n in ('Line', 'LineSegment'):
            return ('axis', _vec(s.Vertexes[0].Point), _vec(c.Direction), com)
        return ('point', com, (0, 0, 0), com)
    srf = s.Surface
    n = type(srf).__name__
    if n == 'Plane':
        u0, u1, v0, v1 = s.ParameterRange
        return ('plane', com, _vec(s.normalAt((u0 + u1) / 2, (v0 + v1) / 2)), com)
    if n in ('Cylinder', 'Toroid', 'Sphere'):
        return ('axis', _vec(srf.Center), _vec(srf.Axis), com)
    if n == 'Cone':
        return ('axis', _vec(srf.Apex), _vec(srf.Axis), com)
    if n == 'SurfaceOfRevolution':
        return ('axis', _vec(srf.Location), _vec(srf.Direction), com)
    return ('point', com, (0, 0, 0), com)
)";

PyObject* sectionModule()
{
    static PyObject* dict = nullptr;
    if (!dict) {
        dict = PyDict_New();
        PyDict_SetItemString(dict, "__builtins__", PyEval_GetBuiltins());
        PyObject* res = PyRun_String(sectionScript, Py_file_input, dict, dict);
        if (!res) {
            Py_DECREF(dict);
            dict = nullptr;
            throw Py::Exception();
        }
        Py_DECREF(res);
    }
    return dict;
}

Base::Vector3d toVector(const Py::Object& obj)
{
    Py::Sequence t(obj);
    return Base::Vector3d(
        static_cast<double>(Py::Float(t[0])),
        static_cast<double>(Py::Float(t[1])),
        static_cast<double>(Py::Float(t[2]))
    );
}

Reference classifyReference(App::DocumentObject* obj, const char* sub)
{
    Reference ref;
    Base::PyGILStateLocker lock;
    try {
        Py::Dict module(sectionModule());
        Py::Callable fn(module.getItem("classify"));
        Py::Tuple args(2);
        args.setItem(0, Py::asObject(obj->getPyObject()));
        args.setItem(1, Py::String(sub ? sub : ""));
        Py::Tuple result(fn.apply(args));

        std::string kind = Py::String(result[0]).as_std_string();
        if (kind == "none") {
            return ref;
        }
        ref.point = toVector(result[1]);
        ref.dir = toVector(result[2]);
        ref.center = toVector(result[3]);
        if (kind == "plane") {
            ref.kind = Reference::Kind::Plane;
        }
        else if (kind == "axis") {
            ref.kind = Reference::Kind::Axis;
        }
        else {
            ref.kind = Reference::Kind::Point;
        }
        if (ref.kind != Reference::Kind::Point) {
            if (ref.dir.Length() < 1e-12) {
                ref.kind = Reference::Kind::Point;
            }
            else {
                ref.dir.Normalize();
            }
        }
    }
    catch (Py::Exception&) {
        Base::PyException e;
        e.reportException();
    }
    return ref;
}

// Like classifyReference(), but also accepts the origin planes (App::Plane),
// which have no shape, using their global placement.
Reference referenceOf(App::DocumentObject* obj, const char* sub)
{
    Reference ref;
    App::DocumentObject* leaf = obj->resolve(sub ? sub : "");
    if (leaf && leaf->isDerivedFrom<App::Plane>()) {
        Base::Placement pl = App::GeoFeature::getGlobalPlacement(leaf, obj, sub ? sub : "");
        ref.kind = Reference::Kind::Plane;
        ref.point = pl.getPosition();
        ref.dir = pl.getRotation().multVec(Base::Vector3d(0, 0, 1));
        ref.dir.Normalize();
        ref.center = ref.point;
        ref.free = true;
        return ref;
    }
    return classifyReference(obj, sub);
}

SbVec3f toSb(const Base::Vector3d& v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

Base::Vector3d toBase(const SbVec3f& v)
{
    return Base::Vector3d(v[0], v[1], v[2]);
}

// Any unit vector perpendicular to dir.
Base::Vector3d perpendicular(const Base::Vector3d& dir)
{
    Base::Vector3d other(1, 0, 0);
    if (std::fabs(dir.x) > std::fabs(dir.y) && std::fabs(dir.x) > std::fabs(dir.z)) {
        other = Base::Vector3d(0, 1, 0);
    }
    Base::Vector3d p = dir % other;
    p.Normalize();
    return p;
}

// Component of v perpendicular to unit vector dir, normalized, or a null vector.
Base::Vector3d rejectFrom(const Base::Vector3d& v, const Base::Vector3d& dir)
{
    Base::Vector3d r = v - dir * (v * dir);
    if (r.Length() < 1e-9) {
        return Base::Vector3d(0, 0, 0);
    }
    r.Normalize();
    return r;
}

SbBox3f modelBox(View3DInventorViewer* viewer, SoNode* exclude)
{
    // exclude the section helpers from the bounding box
    SoGroup* graph = static_cast<SoGroup*>(viewer->getSceneGraph());
    int index = graph->findChild(exclude);
    if (index >= 0) {
        exclude->ref();
        graph->removeChild(index);
    }
    SbBox3f box = viewer->getBoundingBox();
    if (index >= 0) {
        graph->insertChild(exclude, index);
        exclude->unref();
    }
    return box;
}

}  // namespace

/* TRANSLATOR Gui::SectionView */

SectionView::SectionView(View3DInventor* view)
    : QObject(view)
    , SelectionObserver(false, ResolveMode::NoResolve)
    , view(view)
{
    setObjectName(QStringLiteral("SectionView"));
    buildSceneNodes();
    buildPanel();
    loadAppearance();

    if (QWidget* host = view->getViewer()->getWidget()) {
        sketchOverlay = new QPushButton(tr("Create Sketch on Section"), host);
        sketchOverlay->setToolTip(
            tr("Create a sketch on the selected cut face, in the active body if there is one")
        );
        sketchOverlay->hide();
        connect(sketchOverlay, &QPushButton::clicked, this, [this]() {
            createSketch();
            setCapSelected(false);
        });
    }

    capTimer = new QTimer(this);
    capTimer->setSingleShot(true);
    capTimer->setInterval(50);
    connect(capTimer, &QTimer::timeout, this, &SectionView::collectTriangles);

    // clang-format off
    activateViewConnection = Application::Instance->signalActivateView.connect(
        [this](const MDIView* mdi) { onActivateView(mdi); });
    recomputedConnection = App::GetApplication().signalRecomputed.connect(
        [this](const App::Document&) {
            if (enabled) {
                capTimer->start();
            }
        });
    changedObjectConnection = Application::Instance->signalChangedObject.connect(
        [this](const ViewProvider&, const App::Property&) {
            if (enabled) {
                capTimer->start();
            }
        });
    // clang-format on
}

SectionView::~SectionView()
{
    activateViewConnection.disconnect();
    recomputedConnection.disconnect();
    changedObjectConnection.disconnect();
    qApp->removeEventFilter(this);
    detachSelection();
    if (root) {
        root->removeChild(overlay);
        root->removeChild(clip);
        root->unref();
        root = nullptr;
    }
    clip->unref();
    overlay->unref();
    delete dock.data();
}

SectionView* SectionView::of(View3DInventor* view, bool create)
{
    if (!view) {
        return nullptr;
    }
    auto section = view->findChild<SectionView*>(QString(), Qt::FindDirectChildrenOnly);
    if (!section && create) {
        section = new SectionView(view);
    }
    return section;
}

void SectionView::toggle(View3DInventor* view, bool on)
{
    if (auto section = of(view, on)) {
        section->setEnabled(on);
    }
    else if (view) {
        // nothing to turn off, just make sure that the states are right
        if (auto button = view->getViewer()->getWidget()->findChild<QToolButton*>(
                QLatin1String(overlayButtonName)
            )) {
            button->setChecked(false);
        }
    }
}

namespace
{
class OverlayPlacer: public QObject
{
public:
    OverlayPlacer(QWidget* host, QToolButton* button)
        : QObject(button)
        , host(host)
        , button(button)
    {
        host->installEventFilter(this);
        place();
    }
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (obj == host && event->type() == QEvent::Resize) {
            place();
        }
        return false;
    }
    void place()
    {
        button->adjustSize();
        button->move((host->width() - button->width()) / 2, 6);
        button->raise();
    }

private:
    QWidget* host;
    QToolButton* button;
};
}  // namespace

void SectionView::installOverlayButton(View3DInventor* view)
{
    QWidget* host = view->getViewer()->getWidget();
    if (!host) {
        return;
    }
    auto button = new QToolButton(host);
    button->setObjectName(QLatin1String(overlayButtonName));
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setIcon(BitmapFactory().iconFromTheme("Std_SectionView"));
    button->setIconSize(QSize(20, 20));
    button->setToolTip(tr("Section view: cut the model with a movable plane"));
    button->setCursor(Qt::ArrowCursor);
    button->setStyleSheet(
        QStringLiteral(
            "QToolButton { background: transparent; border: 1px solid transparent; "
            "border-radius: 4px; padding: 2px; }"
            "QToolButton:hover { border: 1px solid rgba(128, 128, 128, 160); }"
            "QToolButton:checked { background: transparent; border: 2px solid "
            "rgba(255, 140, 40, 230); padding: 1px; }"
        )
    );
    new OverlayPlacer(host, button);
    button->show();

    QPointer<View3DInventor> guard(view);
    connect(button, &QToolButton::clicked, button, [guard](bool checked) {
        if (!guard) {
            return;
        }
        getMainWindow()->setActiveWindow(guard);
        toggle(guard, checked);
    });
}

void SectionView::buildSceneNodes()
{
    clip = new SoClipPlane();
    clip->ref();

    overlay = new SoSeparator();
    overlay->ref();

    auto events = new SoEventCallback();
    events->addEventCallback(SoMouseButtonEvent::getClassTypeId(), &SectionView::eventCB, this);
    events->addEventCallback(SoLocation2Event::getClassTypeId(), &SectionView::eventCB, this);
    overlay->addChild(events);

    // Hatched cap on the cut area, drawn with the stencil buffer
    overlay->renderCaching = SoSeparator::OFF;
    capNode = new SoCallback();
    capNode->setCallback(&SectionView::capCB, this);
    overlay->addChild(capNode);

    arrowSwitch = new SoSwitch();
    arrowSwitch->whichChild = SO_SWITCH_ALL;
    overlay->addChild(arrowSwitch);

    arrowXform = new SoTransform();

    // Translucent square showing the plane
    auto planeSep = new SoSeparator();
    planeSep->addChild(arrowXform);
    auto pick = new SoPickStyle();
    pick->style = SoPickStyle::UNPICKABLE;
    planeSep->addChild(pick);
    auto light = new SoLightModel();
    light->model = SoLightModel::BASE_COLOR;
    planeSep->addChild(light);
    auto mat = new SoMaterial();
    mat->diffuseColor.setValue(0.9F, 0.4F, 0.1F);
    mat->transparency = 0.85F;
    planeSep->addChild(mat);
    planeCoords = new SoCoordinate3();
    planeSep->addChild(planeCoords);
    auto face = new SoFaceSet();
    face->numVertices.setValue(4);
    planeSep->addChild(face);
    auto lineMat = new SoMaterial();
    lineMat->diffuseColor.setValue(0.9F, 0.4F, 0.1F);
    lineMat->transparency = 0.3F;
    planeSep->addChild(lineMat);
    auto style = new SoDrawStyle();
    style->lineWidth = 1.5F;
    planeSep->addChild(style);
    auto lines = new SoLineSet();
    lines->numVertices.setValue(5);
    planeSep->addChild(lines);
    arrowSwitch->addChild(planeSep);

    // Double arrow along the normal, drawn on top of everything
    auto annotation = new So3DAnnotation();
    arrowRoot = new SoSeparator();
    arrowRoot->addChild(arrowXform);
    arrowScale = new SoScale();
    arrowRoot->addChild(arrowScale);
    auto arrowLight = new SoLightModel();
    arrowLight->model = SoLightModel::PHONG;
    arrowRoot->addChild(arrowLight);
    arrowMaterial = new SoMaterial();
    arrowMaterial->diffuseColor.setValue(0.95F, 0.45F, 0.1F);
    arrowMaterial->transparency = 0.55F;
    arrowRoot->addChild(arrowMaterial);
    auto shaft = new SoCylinder();
    shaft->radius = 0.035F;
    shaft->height = 1.4F;
    arrowRoot->addChild(shaft);
    auto upSep = new SoSeparator();
    auto upMove = new SoTranslation();
    upMove->translation.setValue(0.0F, 0.85F, 0.0F);
    upSep->addChild(upMove);
    auto upCone = new SoCone();
    upCone->bottomRadius = 0.1F;
    upCone->height = 0.3F;
    upSep->addChild(upCone);
    arrowRoot->addChild(upSep);
    auto downSep = new SoSeparator();
    auto downMove = new SoTranslation();
    downMove->translation.setValue(0.0F, -0.85F, 0.0F);
    downSep->addChild(downMove);
    auto downRot = new SoRotation();
    downRot->rotation.setValue(SbVec3f(1, 0, 0), std::numbers::pi_v<float>);
    downSep->addChild(downRot);
    downSep->addChild(upCone);
    arrowRoot->addChild(downSep);
    annotation->addChild(arrowRoot);
    arrowSwitch->addChild(annotation);
}

void SectionView::buildPanel()
{
    auto panel = new QWidget();
    auto layout = new QVBoxLayout(panel);

    auto planeBox = new QGroupBox(tr("References"), panel);
    auto planeLayout = new QVBoxLayout(planeBox);
    refsHint = new QLabel(
        tr("Select faces, edges, vertices or an origin or datum plane in the 3D view or in the "
           "tree to define the section plane. Selecting more adds to the references."),
        planeBox
    );
    refsHint->setWordWrap(true);
    planeLayout->addWidget(refsHint);
    refsContainer = new QWidget(planeBox);
    refsLayout = new QVBoxLayout(refsContainer);
    refsLayout->setContentsMargins(0, 0, 0, 0);
    planeLayout->addWidget(refsContainer);
    auto clearButton = new QPushButton(tr("Clear references"), planeBox);
    clearButton->setToolTip(
        tr("Remove all references and go back to the plane parallel to the screen")
    );
    planeLayout->addWidget(clearButton);
    auto rules = new QLabel(
        tr("• one planar face: plane of the face\n"
           "• two parallel faces: plane midway between them\n"
           "• face + vertex or edge: parallel to the face, through the second reference\n"
           "• circular edge, round face or straight edge: plane from its axis\n"
           "• two vertices: perpendicular to the segment, through its midpoint\n"
           "• three vertices: plane through them\n"
           "• origin or datum plane: that plane"),
        planeBox
    );
    rules->setWordWrap(true);
    planeLayout->addWidget(rules);
    reverseBox = new QCheckBox(tr("Reverse side"), planeBox);
    reverseBox->setToolTip(tr("Keep the other side of the plane"));
    planeLayout->addWidget(reverseBox);
    layout->addWidget(planeBox);

    // Options for axial references (circles, cylinders, straight edges, ...)
    axisWidget = new QGroupBox(tr("Axis"), panel);
    auto axisLayout = new QGridLayout(axisWidget);
    axisMode = new QComboBox(axisWidget);
    axisMode->addItem(tr("Perpendicular to axis"));
    axisMode->addItem(tr("Containing the axis"));
    axisMode->setToolTip(tr("Plane perpendicular to the axis through its center, or plane "
                            "containing the axis"));
    axisLayout->addWidget(axisMode, 0, 0, 1, 2);
    axisLabel = new QLabel(axisWidget);
    axisLabel->setWordWrap(true);
    axisLayout->addWidget(axisLabel, 1, 0, 1, 2);
    axisLayout->addWidget(new QLabel(tr("Angle"), axisWidget), 2, 0);
    angleSpin = new QuantitySpinBox(axisWidget);
    angleSpin->setUnit(Base::Unit::Angle);
    angleSpin->setRange(-360.0, 360.0);
    angleSpin->setSingleStep(5.0);
    angleSpin->setToolTip(tr("Rotation of the plane around the axis"));
    axisLayout->addWidget(angleSpin, 2, 1);
    layout->addWidget(axisWidget);

    auto offsetLayout = new QGridLayout();
    offsetLayout->addWidget(new QLabel(tr("Offset"), panel), 0, 0);
    offsetSpin = new QuantitySpinBox(panel);
    offsetSpin->setUnit(Base::Unit::Length);
    offsetSpin->setRange(-std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
    offsetSpin->setToolTip(tr("Distance of the plane from its reference along the normal"));
    offsetLayout->addWidget(offsetSpin, 0, 1);
    arrowBox = new QCheckBox(tr("Show arrow and plane"), panel);
    arrowBox->setChecked(true);
    arrowBox->setToolTip(tr("Show the arrow to drag the plane and the plane outline"));
    offsetLayout->addWidget(arrowBox, 1, 0, 1, 2);
    capBox = new QCheckBox(tr("Show hatched cut faces"), panel);
    capBox->setChecked(true);
    capBox->setToolTip(tr("Fill the cut area of visible solids with a hatch pattern"));
    offsetLayout->addWidget(capBox, 2, 0, 1, 2);
    layout->addLayout(offsetLayout);

    infoLabel = new QLabel(panel);
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    appearanceBox = new QGroupBox(tr("Appearance"), panel);
    appearanceBox->setCheckable(true);
    appearanceBox->setChecked(false);
    appearanceBox->setToolTip(tr("Appearance of the cut faces"));
    auto appLayout = new QGridLayout(appearanceBox);
    fillButton = new ColorButton(appearanceBox);
    fillButton->setToolTip(tr("Fill color of the cut faces"));
    appLayout->addWidget(new QLabel(tr("Fill color"), appearanceBox), 0, 0);
    appLayout->addWidget(fillButton, 0, 1);
    transparencySpin = new QSpinBox(appearanceBox);
    transparencySpin->setRange(0, 100);
    transparencySpin->setSuffix(QStringLiteral(" %"));
    transparencySpin->setToolTip(tr("Transparency of the fill"));
    appLayout->addWidget(new QLabel(tr("Fill transparency"), appearanceBox), 1, 0);
    appLayout->addWidget(transparencySpin, 1, 1);
    hatchCombo = new QComboBox(appearanceBox);
    hatchCombo->addItem(tr("None"));
    hatchCombo->addItem(tr("Single lines"));
    hatchCombo->addItem(tr("Cross-hatch"));
    appLayout->addWidget(new QLabel(tr("Hatch"), appearanceBox), 2, 0);
    appLayout->addWidget(hatchCombo, 2, 1);
    hatchAngleSpin = new QDoubleSpinBox(appearanceBox);
    hatchAngleSpin->setRange(0.0, 180.0);
    hatchAngleSpin->setDecimals(0);
    hatchAngleSpin->setSuffix(QStringLiteral(" \u00B0"));
    appLayout->addWidget(new QLabel(tr("Hatch angle"), appearanceBox), 3, 0);
    appLayout->addWidget(hatchAngleSpin, 3, 1);
    spacingSpin = new QDoubleSpinBox(appearanceBox);
    spacingSpin->setRange(2.0, 200.0);
    spacingSpin->setDecimals(0);
    spacingSpin->setSuffix(QStringLiteral(" px"));
    appLayout->addWidget(new QLabel(tr("Spacing"), appearanceBox), 4, 0);
    appLayout->addWidget(spacingSpin, 4, 1);
    lineWidthSpin = new QDoubleSpinBox(appearanceBox);
    lineWidthSpin->setRange(0.5, 10.0);
    lineWidthSpin->setDecimals(1);
    lineWidthSpin->setSingleStep(0.5);
    lineWidthSpin->setSuffix(QStringLiteral(" px"));
    appLayout->addWidget(new QLabel(tr("Line width"), appearanceBox), 5, 0);
    appLayout->addWidget(lineWidthSpin, 5, 1);
    lineButton = new ColorButton(appearanceBox);
    lineButton->setToolTip(tr("Color of the hatch lines"));
    appLayout->addWidget(new QLabel(tr("Line color"), appearanceBox), 6, 0);
    appLayout->addWidget(lineButton, 6, 1);
    layout->addWidget(appearanceBox);

    auto sketchButton = new QPushButton(tr("Create Sketch on Plane"), panel);
    sketchButton->setToolTip(
        tr("Create a sketch on the current section plane, in the active body if there is one")
    );
    layout->addWidget(sketchButton);
    auto confirmButton = new QPushButton(tr("Confirm"), panel);
    confirmButton->setToolTip(
        tr("Lock the section: hide the panel, the arrow and the plane and keep only the cut model")
    );
    layout->addWidget(confirmButton);
    auto keysLabel = new QLabel(tr("Esc: turn off the section. Enter: confirm."), panel);
    keysLabel->setWordWrap(true);
    layout->addWidget(keysLabel);
    layout->addStretch();

    dock = new QDockWidget(tr("Section View"), getMainWindow());
    dock->setObjectName(QStringLiteral("Std_SectionView"));
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    dock->setWidget(panel);
    getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();

    // clang-format off
    connect(clearButton, &QPushButton::clicked, this, [this]() { clearReferences(); });
    connect(reverseBox, &QCheckBox::toggled, this, [this](bool on) {
        reversed = on;
        updatePlane();
    });
    connect(axisMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        st.containsAxis = index == 1;
        updateAxisWidgets();
        updatePlane();
    });
    connect(angleSpin, qOverload<double>(&QuantitySpinBox::valueChanged), this, [this](double value) {
        st.angle = value;
        updatePlane();
    });
    connect(offsetSpin, qOverload<double>(&QuantitySpinBox::valueChanged), this, [this](double value) {
        st.offset = value;
        updatePlane();
    });
    connect(arrowBox, &QCheckBox::toggled, this, [this](bool) { updateArrowVisibility(); });
    connect(appearanceBox, &QGroupBox::toggled, this, [this](bool on) {
        const auto children = appearanceBox->findChildren<QWidget*>(
            QString(),
            Qt::FindDirectChildrenOnly
        );
        for (auto child : children) {
            child->setVisible(on);
        }
    });
    connect(fillButton, &ColorButton::changed, this, [this]() { appearanceChanged(); });
    connect(lineButton, &ColorButton::changed, this, [this]() { appearanceChanged(); });
    connect(transparencySpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        appearanceChanged();
    });
    connect(hatchCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        appearanceChanged();
    });
    for (auto spin : {hatchAngleSpin, spacingSpin, lineWidthSpin}) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) {
            appearanceChanged();
        });
    }
    connect(confirmButton, &QPushButton::clicked, this, &SectionView::confirm);
    connect(capBox, &QCheckBox::toggled, this, [this](bool on) {
        showCap = on;
        if (on) {
            collectTriangles();
        }
        if (view) {
            view->getViewer()->redraw();
        }
    });
    connect(sketchButton, &QPushButton::clicked, this, &SectionView::createSketch);
    // clang-format on

    const auto appChildren = appearanceBox->findChildren<QWidget*>(
        QString(),
        Qt::FindDirectChildrenOnly
    );
    for (auto child : appChildren) {
        child->hide();
    }
    updateAxisWidgets();
}

namespace
{
ParameterGrp::handle sectionParams()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View/SectionView"
    );
}
}  // namespace

void SectionView::loadAppearance()
{
    auto grp = sectionParams();
    CapStyle d;
    auto color = [&](const char* name, const QColor& def) {
        unsigned long packed = grp->GetUnsigned(name, def.rgb() & 0xFFFFFF);
        return QColor(
            static_cast<int>((packed >> 16) & 0xFF),
            static_cast<int>((packed >> 8) & 0xFF),
            static_cast<int>(packed & 0xFF)
        );
    };
    capStyle.fill = color("CapFillColor", d.fill);
    capStyle.line = color("CapLineColor", d.line);
    capStyle.transparency = std::clamp<int>(grp->GetInt("CapTransparency", d.transparency), 0, 100);
    capStyle.hatch = std::clamp<int>(grp->GetInt("CapHatchStyle", d.hatch), 0, 2);
    capStyle.angle = std::clamp(grp->GetFloat("CapHatchAngle", d.angle), 0.0, 180.0);
    capStyle.spacing = std::clamp(grp->GetFloat("CapHatchSpacing", d.spacing), 2.0, 200.0);
    capStyle.lineWidth = std::clamp(grp->GetFloat("CapLineWidth", d.lineWidth), 0.5, 10.0);

    const QSignalBlocker b1(fillButton);
    const QSignalBlocker b2(lineButton);
    const QSignalBlocker b3(transparencySpin);
    const QSignalBlocker b4(hatchCombo);
    const QSignalBlocker b5(hatchAngleSpin);
    const QSignalBlocker b6(spacingSpin);
    const QSignalBlocker b7(lineWidthSpin);
    fillButton->setColor(capStyle.fill);
    lineButton->setColor(capStyle.line);
    transparencySpin->setValue(capStyle.transparency);
    hatchCombo->setCurrentIndex(capStyle.hatch);
    hatchAngleSpin->setValue(capStyle.angle);
    spacingSpin->setValue(capStyle.spacing);
    lineWidthSpin->setValue(capStyle.lineWidth);
}

void SectionView::saveAppearance()
{
    auto grp = sectionParams();
    auto pack = [](const QColor& c) {
        return static_cast<unsigned long>((c.red() << 16) | (c.green() << 8) | c.blue());
    };
    grp->SetUnsigned("CapFillColor", pack(capStyle.fill));
    grp->SetUnsigned("CapLineColor", pack(capStyle.line));
    grp->SetInt("CapTransparency", capStyle.transparency);
    grp->SetInt("CapHatchStyle", capStyle.hatch);
    grp->SetFloat("CapHatchAngle", capStyle.angle);
    grp->SetFloat("CapHatchSpacing", capStyle.spacing);
    grp->SetFloat("CapLineWidth", capStyle.lineWidth);
}

void SectionView::appearanceChanged()
{
    capStyle.fill = fillButton->color();
    capStyle.line = lineButton->color();
    capStyle.transparency = transparencySpin->value();
    capStyle.hatch = hatchCombo->currentIndex();
    capStyle.angle = hatchAngleSpin->value();
    capStyle.spacing = spacingSpin->value();
    capStyle.lineWidth = lineWidthSpin->value();
    saveAppearance();
    if (view) {
        view->getViewer()->redraw();
    }
}

void SectionView::updateArrowVisibility()
{
    bool show = arrowBox->isChecked() && !confirmed;
    arrowSwitch->whichChild = show ? SO_SWITCH_ALL : SO_SWITCH_NONE;
    dragging = dragging && show;
}

void SectionView::confirm()
{
    if (!enabled) {
        return;
    }
    confirmed = true;
    dragging = false;
    capPressed = false;
    setArrowHighlight(false);
    updateArrowVisibility();
    dock->hide();
    if (view) {
        view->getViewer()->redraw();
    }
    updateToggleStates();
}

void SectionView::editAgain()
{
    if (!enabled) {
        return;
    }
    confirmed = false;
    setCapSelected(false);
    updateArrowVisibility();
    rebuildReferenceList();
    dock->show();
    dock->raise();
    if (view) {
        view->getViewer()->redraw();
    }
    updateToggleStates();
}

void SectionView::setEnabled(bool on)
{
    if (on != enabled && view && dock) {
        View3DInventorViewer* viewer = view->getViewer();
        if (on) {
            root = static_cast<SoGroup*>(viewer->getSceneGraph());
            root->ref();
            int index = -1;
            if (auto editingRoot = viewer->getEditingRoot()) {
                index = root->findChild(editingRoot);
            }
            // the helpers go before the clip plane so they are not clipped
            root->insertChild(clip, index + 1);
            root->insertChild(overlay, index + 1);
            enabled = true;
            confirmed = false;
            // start from the default plane, then follow the selection
            setDefaultPlane();
            qApp->installEventFilter(this);
            attachSelection();
            updateFromSelection(false);
            updateArrowVisibility();
            collectTriangles();
            dock->show();
            dock->raise();
        }
        else {
            qApp->removeEventFilter(this);
            detachSelection();
            setCapSelected(false);
            capTimer->stop();
            dragging = false;
            root->removeChild(overlay);
            root->removeChild(clip);
            root->unref();
            root = nullptr;
            enabled = false;
            confirmed = false;
            dock->hide();
        }
    }
    updateToggleStates();
}

void SectionView::updateToggleStates()
{
    if (!view) {
        return;
    }
    if (auto button = view->getViewer()->getWidget()->findChild<QToolButton*>(
            QLatin1String(overlayButtonName)
        )) {
        button->setChecked(enabled);
    }
    if (getMainWindow()->activeWindow() == view) {
        Command* cmd = Application::Instance->commandManager().getCommandByName("Std_SectionView");
        if (cmd && cmd->getAction()) {
            cmd->getAction()->setBlockedChecked(enabled);
        }
    }
}

void SectionView::onActivateView(const QWidget* activated)
{
    if (!enabled || !dock) {
        return;
    }
    dock->setVisible(activated == view && !confirmed);
}

void SectionView::setDefaultPlane()
{
    if (!view) {
        return;
    }
    // Without references the plane is parallel to the screen, through the center
    // of the model, so that the cut is immediately visible and can be moved
    // with the arrow.
    View3DInventorViewer* viewer = view->getViewer();
    st = PlaneState();
    st.source = Source::View;
    SbVec3f dir = viewer->getViewDirection();
    dir.normalize();
    st.normal = toBase(dir);
    SbBox3f box = modelBox(viewer, overlay);
    if (!box.isEmpty()) {
        st.basePoint = toBase(box.getCenter());
    }
    else if (SoCamera* cam = viewer->getSoRenderManager()->getCamera()) {
        st.basePoint = toBase(cam->position.getValue() + dir * cam->focalDistance.getValue());
    }
    st.offset = 0.0;
    infoLabel->setText(tr("No reference: plane parallel to the screen, through the model center"));
    applyState();
}

void SectionView::applyState()
{
    axisMode->blockSignals(true);
    axisMode->setCurrentIndex(st.containsAxis ? 1 : 0);
    axisMode->blockSignals(false);
    angleSpin->blockSignals(true);
    angleSpin->setValue(st.angle);
    angleSpin->blockSignals(false);
    updateAxisWidgets();
    updateStepFromModel();
    updateArrowSize();
    updateOffsetSpin();
    updatePlane();
}

void SectionView::onSelectionChanged(const SelectionChanges& msg)
{
    if (!enabled || confirmed) {
        return;
    }
    if (msg.Type == SelectionChanges::AddSelection || msg.Type == SelectionChanges::RmvSelection
        || msg.Type == SelectionChanges::ClrSelection || msg.Type == SelectionChanges::SetSelection) {
        updateFromSelection(false);
    }
}

void SectionView::updateFromSelection(bool resetIfEmpty)
{
    if (!view) {
        return;
    }
    rebuildReferenceList();
    if (planeFromSelection()) {
        return;
    }
    if (refItems.empty()) {
        if (resetIfEmpty) {
            setDefaultPlane();
        }
        else {
            // e.g. a click in the empty space: keep the current plane
            infoLabel->setText(tr("No reference selected: the plane stays where it is"));
        }
    }
}

void SectionView::rebuildReferenceList()
{
    refItems.clear();
    for (const auto& sel : Selection().getCompleteSelection(ResolveMode::NoResolve)) {
        if (!sel.pObject) {
            continue;
        }
        RefItem item;
        item.doc = sel.DocName ? sel.DocName : "";
        item.obj = sel.FeatName ? sel.FeatName : "";
        item.sub = sel.SubName ? sel.SubName : "";
        App::DocumentObject* leaf = sel.pObject->resolve(item.sub.c_str());
        // the readable index name (Face1), not the mapped element name
        const std::string oldName = Data::oldElementName(item.sub.c_str());
        const char* element = Data::findElementName(oldName.c_str());
        QString text = QString::fromUtf8((leaf ? leaf : sel.pObject)->Label.getValue());
        if (element && *element) {
            text += QStringLiteral(" \u2022 ") + QString::fromUtf8(element);
        }
        item.text = text;
        refItems.push_back(item);
    }

    while (QLayoutItem* child = refsLayout->takeAt(0)) {
        if (QWidget* w = child->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete child;
    }
    for (const auto& item : refItems) {
        auto row = new QWidget(refsContainer);
        auto rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(new QLabel(item.text, row), 1);
        auto remove = new QToolButton(row);
        remove->setText(QStringLiteral("\u00D7"));
        remove->setToolTip(tr("Remove this reference"));
        rowLayout->addWidget(remove);
        refsLayout->addWidget(row);
        RefItem copy = item;
        connect(remove, &QToolButton::clicked, this, [this, copy]() { removeReference(copy); });
    }
    refsHint->setVisible(refItems.empty());
}

void SectionView::removeReference(const RefItem& item)
{
    Selection().rmvSelection(item.doc.c_str(), item.obj.c_str(), item.sub.c_str());
    // removing the last reference goes back to the default plane
    updateFromSelection(true);
}

void SectionView::clearReferences()
{
    Selection().clearCompleteSelection();
    updateFromSelection(true);
}

bool SectionView::planeFromSelection()
{
    std::vector<Reference> refs;
    for (const auto& sel : Selection().getCompleteSelection(ResolveMode::NoResolve)) {
        if (!sel.pObject) {
            continue;
        }
        Reference ref = referenceOf(sel.pObject, sel.SubName);
        if (ref.kind != Reference::Kind::None) {
            refs.push_back(ref);
        }
    }
    if (refs.empty()) {
        if (!Selection().getCompleteSelection(ResolveMode::NoResolve).empty()) {
            infoLabel->setText(tr("The selection does not define a plane"));
        }
        return false;
    }

    std::vector<const Reference*> planes;
    std::vector<const Reference*> axes;
    std::vector<const Reference*> points;
    for (const auto& ref : refs) {
        switch (ref.kind) {
            case Reference::Kind::Plane:
                planes.push_back(&ref);
                break;
            case Reference::Kind::Axis:
                axes.push_back(&ref);
                break;
            default:
                points.push_back(&ref);
                break;
        }
    }

    PlaneState s = st;
    s.source = Source::Selection;
    s.offset = 0.0;
    s.axial = false;
    s.hasSecondRef = false;
    QString message;

    auto otherThan = [&](const Reference* exclude) -> const Reference* {
        for (const auto& ref : refs) {
            if (&ref != exclude) {
                return &ref;
            }
        }
        return nullptr;
    };

    if (!planes.empty()) {
        const Reference* face = planes.front();
        // the normal points into the material, so that at offset zero the face
        // is on the plane and a positive offset cuts into the solid
        s.normal = -face->dir;
        s.basePoint = face->point;
        message = tr("Plane of the selected face");
        if (face->free) {
            // origin planes have no material: keep the side away from the viewer
            Base::Vector3d viewDir = toBase(view->getViewer()->getViewDirection());
            s.normal = (face->dir * viewDir >= 0.0) ? face->dir : -face->dir;
            message = tr("Selected plane");
        }
        if (planes.size() >= 2 && std::fabs(planes[0]->dir * planes[1]->dir) > 0.9999) {
            Base::Vector3d mid = (planes[0]->point + planes[1]->point) / 2.0;
            s.basePoint = mid;
            message = tr("Plane midway between the two parallel faces");
        }
        else if (const Reference* other = otherThan(face)) {
            s.basePoint = other->center;
            message = tr("Plane parallel to the face, through the second reference");
        }
    }
    else if (!axes.empty()) {
        const Reference* axis = axes.front();
        s.axial = true;
        s.axisDir = axis->dir;
        // put the base point on the axis, at the height of the element center
        s.axisPoint = axis->point + s.axisDir * ((axis->center - axis->point) * s.axisDir);

        Base::Vector3d inPlane(0, 0, 0);
        if (const Reference* other = otherThan(axis)) {
            inPlane = rejectFrom(other->center - s.axisPoint, s.axisDir);
            if (inPlane.Length() < 0.5 && other->kind != Reference::Kind::Point) {
                inPlane = rejectFrom(other->dir, s.axisDir);
            }
        }
        if (inPlane.Length() > 0.5) {
            s.hasSecondRef = true;
            s.containsAxis = true;
            s.angle = 0.0;
            message = tr("Plane containing the axis and the second reference");
        }
        else {
            // plane containing the axis that faces the viewer as much as possible
            Base::Vector3d viewDir = toBase(view->getViewer()->getViewDirection());
            Base::Vector3d n = rejectFrom(viewDir, s.axisDir);
            inPlane = n.Length() > 0.5 ? n % s.axisDir : perpendicular(s.axisDir);
            inPlane.Normalize();
            message = tr("Plane from the axis of the selected reference");
        }
        s.axisRef = inPlane;
    }
    else if (points.size() >= 3) {
        Base::Vector3d p0 = points[0]->point;
        Base::Vector3d n = (points[1]->point - p0) % (points[2]->point - p0);
        if (n.Length() < 1e-9) {
            return false;
        }
        n.Normalize();
        s.normal = n;
        s.basePoint = p0;
        message = tr("Plane through three points");
    }
    else if (points.size() == 2) {
        Base::Vector3d d = points[1]->point - points[0]->point;
        if (d.Length() < 1e-9) {
            return false;
        }
        d.Normalize();
        s.normal = d;
        s.basePoint = (points[0]->point + points[1]->point) / 2.0;
        message = tr("Plane perpendicular to the segment, through its midpoint");
    }
    else {
        infoLabel->setText(tr("One vertex is not enough: select another vertex or a face…"));
        return false;
    }

    st = s;
    infoLabel->setText(message);
    applyState();
    return true;
}

void SectionView::updateAxisWidgets()
{
    axisWidget->setVisible(st.axial);
    angleSpin->setEnabled(st.containsAxis);
    if (!st.containsAxis) {
        axisLabel->setText(tr("The plane is perpendicular to the axis, through its center."));
    }
    else if (st.hasSecondRef) {
        axisLabel->setText(
            tr("The plane contains the axis and passes through the second reference; "
               "the angle rotates it around the axis.")
        );
    }
    else {
        axisLabel->setText(
            tr("Select a second reference to fix the rotation around the axis, or type the "
               "angle.")
        );
    }
}

void SectionView::updateStepFromModel()
{
    SbBox3f box = modelBox(view->getViewer(), overlay);
    modelSize = box.isEmpty() ? 10.0 : (box.getMax() - box.getMin()).length();
    modelSize = std::max(modelSize, 1e-3);
    double step = std::pow(10.0, std::floor(std::log10(modelSize / 100.0)));
    offsetSpin->setSingleStep(step);
}

void SectionView::updateArrowSize()
{
    auto scale = static_cast<float>(modelSize * 0.12);
    arrowScale->scaleFactor.setValue(scale, scale, scale);

    auto h = static_cast<float>(modelSize * 0.6);
    planeCoords->point.setNum(5);
    planeCoords->point.set1Value(0, -h, 0, -h);
    planeCoords->point.set1Value(1, h, 0, -h);
    planeCoords->point.set1Value(2, h, 0, h);
    planeCoords->point.set1Value(3, -h, 0, h);
    planeCoords->point.set1Value(4, -h, 0, -h);
}

Base::Vector3d SectionView::planePoint() const
{
    return st.basePoint + st.normal * st.offset;
}

Base::Vector3d SectionView::clipNormal() const
{
    return reversed ? -st.normal : st.normal;
}

void SectionView::updatePlane()
{
    if (st.axial) {
        if (st.containsAxis) {
            double rad = Base::toRadians(st.angle);
            Base::Vector3d dir = st.axisRef * std::cos(rad)
                + (st.axisDir % st.axisRef) * std::sin(rad);
            st.normal = st.axisDir % dir;
            st.normal.Normalize();
        }
        else {
            st.normal = st.axisDir;
        }
        st.basePoint = st.axisPoint;
    }

    clip->plane.setValue(SbPlane(toSb(clipNormal()), toSb(planePoint())));
    clip->on = true;

    arrowXform->translation.setValue(toSb(planePoint()));
    arrowXform->rotation.setValue(SbRotation(SbVec3f(0, 1, 0), toSb(st.normal)));
}

void SectionView::updateOffsetSpin()
{
    offsetSpin->blockSignals(true);
    offsetSpin->setValue(st.offset);
    offsetSpin->blockSignals(false);
}

namespace
{
struct TriangleCollector
{
    std::vector<float>* tris;
    SoNode* exclude;
    View3DInventorViewer* viewer;
    std::map<ViewProvider*, bool> solidCache;
    bool accept {false};
};

// Only objects with solids give a meaningful cap: sheets and sketch faces
// would fill their whole projection.
bool hasSolids(ViewProvider* vp)
{
    auto vpd = freecad_cast<ViewProviderDocumentObject*>(vp);
    if (!vpd || !vpd->getObject()) {
        return false;
    }
    Base::PyGILStateLocker lock;
    try {
        Py::Object obj(vpd->getObject()->getPyObject(), true);
        if (!obj.hasAttr("Shape")) {
            return false;
        }
        Py::Object shape = obj.getAttr("Shape");
        Py::Sequence solids(shape.getAttr("Solids"));
        return solids.size() > 0;
    }
    catch (Py::Exception&) {
        PyErr_Clear();
        return false;
    }
}

SoCallbackAction::Response preSeparatorCB(void* data, SoCallbackAction*, const SoNode* node)
{
    auto collector = static_cast<TriangleCollector*>(data);
    return node == collector->exclude ? SoCallbackAction::PRUNE : SoCallbackAction::CONTINUE;
}

SoCallbackAction::Response preShapeCB(void* data, SoCallbackAction* action, const SoNode* node)
{
    // only the faces of shapes (closed solids give correct caps); datum planes,
    // markers and other helpers are ignored
    static const SoType brepType = SoType::fromName("SoBrepFaceSet");
    auto collector = static_cast<TriangleCollector*>(data);
    collector->accept = !brepType.isBad() && node->getTypeId().isDerivedFrom(brepType);
    if (collector->accept) {
        auto path = const_cast<SoPath*>(action->getCurPath());
        ViewProvider* vp = collector->viewer->getViewProviderByPathFromTail(path);
        auto it = collector->solidCache.find(vp);
        if (it == collector->solidCache.end()) {
            it = collector->solidCache.emplace(vp, hasSolids(vp)).first;
        }
        collector->accept = it->second;
    }
    return SoCallbackAction::CONTINUE;
}

void triangleCB(
    void* data,
    SoCallbackAction* action,
    const SoPrimitiveVertex* v1,
    const SoPrimitiveVertex* v2,
    const SoPrimitiveVertex* v3
)
{
    auto collector = static_cast<TriangleCollector*>(data);
    if (!collector->accept) {
        return;
    }
    const SbMatrix& matrix = action->getModelMatrix();
    for (const SoPrimitiveVertex* v : {v1, v2, v3}) {
        SbVec3f p;
        matrix.multVecMatrix(v->getPoint(), p);
        collector->tris->push_back(p[0]);
        collector->tris->push_back(p[1]);
        collector->tris->push_back(p[2]);
    }
}

}  // namespace

void SectionView::collectTriangles()
{
    capTris.clear();
    if (!enabled || !view || !showCap) {
        return;
    }
    View3DInventorViewer* viewer = view->getViewer();
    TriangleCollector collector {&capTris, overlay, viewer, {}};
    SoCallbackAction action(viewer->getSoRenderManager()->getViewportRegion());
    action.addPreCallback(SoSeparator::getClassTypeId(), preSeparatorCB, &collector);
    action.addPreCallback(SoShape::getClassTypeId(), preShapeCB, &collector);
    action.addTriangleCallback(SoShape::getClassTypeId(), triangleCB, &collector);
    action.apply(viewer->getSceneGraph());
    viewer->redraw();
}

void SectionView::capCB(void* data, SoAction* action)
{
    if (action->isOfType(SoGLRenderAction::getClassTypeId())) {
        static_cast<SectionView*>(data)->renderCap(static_cast<SoGLRenderAction*>(action));
    }
}

void SectionView::renderCap(SoGLRenderAction* action)
{
    Q_UNUSED(action);
    if (!enabled || !showCap || capTris.empty()) {
        return;
    }
    GLint stencilBits = 0;
    glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
    if (stencilBits < 1) {
        return;
    }

    Base::Vector3d n = clipNormal();
    Base::Vector3d p = planePoint();
    double d = n * p;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // 1. parity of the clipped geometry along each view ray: odd inside a solid
    GLdouble equation[4] = {n.x, n.y, n.z, -d};
    glClipPlane(GL_CLIP_PLANE0, equation);
    glEnable(GL_CLIP_PLANE0);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x01);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0x01);
    glStencilOp(GL_KEEP, GL_INVERT, GL_INVERT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, capTris.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(capTris.size() / 3));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_CLIP_PLANE0);

    // 2. a large quad on the plane where the parity is odd
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // where the plane quad passes the depth test the stencil goes from 1 to 2,
    // so the hatch lines can be limited to the visible part of the cap
    glStencilMask(0xFF);
    glStencilFunc(GL_EQUAL, 1, 0x01);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    Base::Vector3d u = perpendicular(n);
    Base::Vector3d v = n % u;
    double h = modelSize * 2.0;
    const Base::Vector3d corners[4] = {
        p - u * h - v * h,
        p + u * h - v * h,
        p + u * h + v * h,
        p - u * h + v * h
    };
    auto drawQuad = [&]() {
        glBegin(GL_QUADS);
        for (const auto& c : corners) {
            glVertex3d(c.x, c.y, c.z);
        }
        glEnd();
    };
    const CapStyle& cs = capStyle;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    QColor fillColor = cs.fill;
    if (capSelected) {
        // the cut face picked for a sketch is tinted
        fillColor = QColor(
            (fillColor.red() + 60) / 2,
            (fillColor.green() + 220) / 2,
            (fillColor.blue() + 60) / 2
        );
    }
    glColor4f(
        static_cast<float>(fillColor.redF()),
        static_cast<float>(fillColor.greenF()),
        static_cast<float>(fillColor.blueF()),
        1.0F - static_cast<float>(cs.transparency) / 100.0F
    );
    drawQuad();

    // 3. hatch lines in screen space, only where the cap is visible (stencil == 2)
    if (cs.hatch > 0) {
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        double dpr = view ? view->devicePixelRatioF() : 1.0;
        double spacing = std::max(2.0, cs.spacing * dpr);
        double w = vp[2];
        double hgt = vp[3];
        double diag = std::sqrt(w * w + hgt * hgt);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, w, 0.0, hgt, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glStencilMask(0x00);
        glStencilFunc(GL_EQUAL, 2, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glLineWidth(static_cast<float>(cs.lineWidth * dpr));
        glColor4f(
            static_cast<float>(cs.line.redF()),
            static_cast<float>(cs.line.greenF()),
            static_cast<float>(cs.line.blueF()),
            1.0F
        );
        auto drawFamily = [&](double degrees) {
            double a = Base::toRadians(degrees);
            double dx = std::cos(a);
            double dy = std::sin(a);
            double mx = -dy;
            double my = dx;
            // range of the perpendicular offset over the viewport corners
            double lo = std::min({0.0, mx * w, my * hgt, mx * w + my * hgt});
            double hi = std::max({0.0, mx * w, my * hgt, mx * w + my * hgt});
            double start = std::ceil(lo / spacing) * spacing;
            long count = static_cast<long>((hi - start) / spacing) + 1;
            if (count > 20000) {
                return;
            }
            glBegin(GL_LINES);
            for (long i = 0; i < count; ++i) {
                double t = start + static_cast<double>(i) * spacing;
                double cx = mx * t;
                double cy = my * t;
                glVertex2d(cx - dx * diag, cy - dy * diag);
                glVertex2d(cx + dx * diag, cy + dy * diag);
            }
            glEnd();
        };
        drawFamily(cs.angle);
        if (cs.hatch == 2) {
            drawFamily(cs.angle + 90.0);
        }

        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    glStencilMask(0xFF);
    glClear(GL_STENCIL_BUFFER_BIT);

    glPopClientAttrib();
    glPopAttrib();
}

void SectionView::setArrowHighlight(bool on)
{
    if (on == highlighted) {
        return;
    }
    highlighted = on;
    arrowMaterial->transparency = on ? 0.0F : 0.55F;
}

void SectionView::eventCB(void* data, SoEventCallback* cb)
{
    auto self = static_cast<SectionView*>(data);
    if (!self->enabled || !self->view) {
        return;
    }
    if (self->confirmed) {
        self->handleCapEvent(cb);
        return;
    }
    if (self->arrowSwitch->whichChild.getValue() == SO_SWITCH_NONE) {
        return;
    }
    const SoEvent* ev = cb->getEvent();
    View3DInventorViewer* viewer = self->view->getViewer();
    SbVec3f p1;
    SbVec3f p2;
    viewer->projectPointToLine(ev->getPosition(), p1, p2);
    SbVec3f rayDir = p2 - p1;
    if (rayDir.normalize() == 0.0F) {
        return;
    }

    auto hitArrow = [&]() {
        SoRayPickAction pick(viewer->getSoRenderManager()->getViewportRegion());
        pick.setRay(p1, rayDir);
        pick.apply(self->arrowRoot);
        return pick.getPickedPoint() != nullptr;
    };
    auto axisParam = [&]() {
        SbVec3f base = toSb(self->st.basePoint);
        SbVec3f n = toSb(self->st.normal);
        SbLine axis(base, base + n);
        SbLine ray(p1, p1 + rayDir);
        SbVec3f onAxis;
        SbVec3f onRay;
        if (!axis.getClosestPoints(ray, onAxis, onRay)) {
            return self->dragStartParam;
        }
        return static_cast<double>((onAxis - base).dot(n));
    };

    if (SoMouseButtonEvent::isButtonPressEvent(ev, SoMouseButtonEvent::BUTTON1)) {
        if (hitArrow()) {
            self->dragging = true;
            self->dragStartOffset = self->st.offset;
            self->dragStartParam = axisParam();
            self->setArrowHighlight(true);
            cb->setHandled();
        }
    }
    else if (SoMouseButtonEvent::isButtonReleaseEvent(ev, SoMouseButtonEvent::BUTTON1)) {
        if (self->dragging) {
            self->dragging = false;
            self->setArrowHighlight(hitArrow());
            cb->setHandled();
        }
    }
    else if (ev->isOfType(SoLocation2Event::getClassTypeId())) {
        if (self->dragging) {
            self->st.offset = self->dragStartOffset + axisParam() - self->dragStartParam;
            self->updateOffsetSpin();
            self->updatePlane();
            cb->setHandled();
        }
        else {
            self->setArrowHighlight(hitArrow());
        }
    }
}

bool SectionView::capAt(const SoEvent* ev) const
{
    if (!view || !showCap || capTris.empty()) {
        return false;
    }
    SbVec3f p1;
    SbVec3f p2;
    view->getViewer()->projectPointToLine(ev->getPosition(), p1, p2);
    SbVec3f dir = p2 - p1;
    if (dir.normalize() == 0.0F) {
        return false;
    }
    const Base::Vector3d n = clipNormal();
    const double d = n * planePoint();
    const double o[3] = {p1[0], p1[1], p1[2]};
    const double r[3] = {dir[0], dir[1], dir[2]};
    const double denom = n.x * r[0] + n.y * r[1] + n.z * r[2];
    if (std::fabs(denom) < 1e-9) {
        return false;
    }
    const double tPlane = (d - (n.x * o[0] + n.y * o[1] + n.z * o[2])) / denom;
    if (tPlane < 0.0) {
        return false;
    }
    const double eps = modelSize * 1e-6;

    // The cap is where the ray meets the plane inside a solid. It is hidden when the
    // ray first crosses material that is kept; the point is inside a solid when the
    // ray crosses its boundary an odd number of times beyond the plane.
    int beyond = 0;
    const size_t count = capTris.size() / 9;
    for (size_t i = 0; i < count; ++i) {
        const float* tri = &capTris[i * 9];
        const double a[3] = {tri[0], tri[1], tri[2]};
        const double e1[3] = {tri[3] - a[0], tri[4] - a[1], tri[5] - a[2]};
        const double e2[3] = {tri[6] - a[0], tri[7] - a[1], tri[8] - a[2]};
        const double pv[3] = {
            r[1] * e2[2] - r[2] * e2[1],
            r[2] * e2[0] - r[0] * e2[2],
            r[0] * e2[1] - r[1] * e2[0]
        };
        const double det = e1[0] * pv[0] + e1[1] * pv[1] + e1[2] * pv[2];
        if (std::fabs(det) < 1e-18) {
            continue;
        }
        const double tv[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
        const double u = (tv[0] * pv[0] + tv[1] * pv[1] + tv[2] * pv[2]) / det;
        if (u < 0.0 || u > 1.0) {
            continue;
        }
        const double qv[3] = {
            tv[1] * e1[2] - tv[2] * e1[1],
            tv[2] * e1[0] - tv[0] * e1[2],
            tv[0] * e1[1] - tv[1] * e1[0]
        };
        const double v = (r[0] * qv[0] + r[1] * qv[1] + r[2] * qv[2]) / det;
        if (v < 0.0 || u + v > 1.0) {
            continue;
        }
        const double t = (e2[0] * qv[0] + e2[1] * qv[1] + e2[2] * qv[2]) / det;
        if (t < 0.0) {
            continue;
        }
        if (t > tPlane + eps) {
            ++beyond;
        }
        else if (t < tPlane - eps) {
            const double side = n.x * (o[0] + r[0] * t) + n.y * (o[1] + r[1] * t)
                + n.z * (o[2] + r[2] * t) - d;
            if (side >= 0.0) {
                return false;  // kept geometry in front of the cap
            }
        }
    }
    return beyond % 2 == 1;
}

void SectionView::handleCapEvent(SoEventCallback* cb)
{
    const SoEvent* ev = cb->getEvent();
    if (SoMouseButtonEvent::isButtonPressEvent(ev, SoMouseButtonEvent::BUTTON1)) {
        capPressed = capAt(ev);
        if (capPressed) {
            cb->setHandled();
        }
    }
    else if (SoMouseButtonEvent::isButtonReleaseEvent(ev, SoMouseButtonEvent::BUTTON1)) {
        if (capPressed) {
            capPressed = false;
            cb->setHandled();
            if (capAt(ev)) {
                bool twice = capSelected && capClickTimer.isValid()
                    && capClickTimer.elapsed() < QApplication::doubleClickInterval();
                if (twice) {
                    // double click on the cut face: sketch on it
                    capClickTimer.invalidate();
                    setCapSelected(false);
                    createSketch();
                }
                else {
                    setCapSelected(true);
                    capClickTimer.start();
                }
            }
        }
        else if (capSelected) {
            setCapSelected(false);
        }
    }
}

void SectionView::setCapSelected(bool on)
{
    if (capSelected == on) {
        return;
    }
    capSelected = on;
    updateSketchButton();
    if (view) {
        view->getViewer()->redraw();
    }
}

void SectionView::updateSketchButton()
{
    if (!sketchOverlay || !view) {
        return;
    }
    QWidget* host = view->getViewer()->getWidget();
    bool show = enabled && confirmed && capSelected;
    if (show && host) {
        sketchOverlay->adjustSize();
        sketchOverlay->move((host->width() - sketchOverlay->width()) / 2, 40);
        sketchOverlay->raise();
    }
    sketchOverlay->setVisible(show);
}

bool SectionView::editingBlocked() const
{
    return !view || QApplication::activeModalWidget() || QApplication::activePopupWidget()
        || Gui::Control().activeDialog() || view->getViewer()->isEditing()
        || getMainWindow()->activeWindow() != view;
}

bool SectionView::handleKey(QKeyEvent* event, bool shortcutOverride)
{
    if (!enabled || !view) {
        return false;
    }
    const bool esc = event->key() == Qt::Key_Escape;
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (!esc && !enter) {
        return false;
    }
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    // never steal the keys from task dialogs, sketch editing, popups or modal dialogs
    if (editingBlocked()) {
        return false;
    }
    QWidget* focus = QApplication::focusWidget();
    if (!focus) {
        return false;
    }
    const bool inView = focus == view || view->isAncestorOf(focus);
    const bool inDock = dock && (focus == dock || dock->isAncestorOf(focus));
    if (!inView && !inDock) {
        return false;
    }
    if (confirmed) {
        // only deselects a picked cut face
        if (esc && capSelected && inView) {
            if (!shortcutOverride) {
                setCapSelected(false);
            }
            return true;
        }
        return false;
    }
    if (!shortcutOverride) {
        QTimer::singleShot(0, this, [this, esc]() {
            if (!enabled || confirmed) {
                return;
            }
            if (esc) {
                toggle(view, false);
            }
            else {
                confirm();
            }
        });
    }
    return true;
}

bool SectionView::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
        case QEvent::ShortcutOverride:
            if (handleKey(static_cast<QKeyEvent*>(event), true)) {
                // deliver the key to us instead of triggering a shortcut
                event->accept();
            }
            return false;
        case QEvent::KeyPress:
            return handleKey(static_cast<QKeyEvent*>(event), false);
        case QEvent::Resize:
            if (view && sketchOverlay && sketchOverlay->isVisible()
                && watched == view->getViewer()->getWidget()) {
                updateSketchButton();
            }
            return false;
        default:
            return false;
    }
}

void SectionView::createSketch()
{
    if (!view || !enabled) {
        return;
    }
    getMainWindow()->setActiveWindow(view);
    // the sketch normal points away from the kept material
    Base::Vector3d p = planePoint();
    Base::Vector3d n = -clipNormal();

    std::ostringstream str;
    str << std::setprecision(15);
    str << "_pl = FreeCAD.Placement(FreeCAD.Vector(" << p.x << ", " << p.y << ", " << p.z
        << "), FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), FreeCAD.Vector(" << n.x << ", " << n.y
        << ", " << n.z << ")))\n"
        << "_body = FreeCADGui.ActiveDocument.ActiveView.getActiveObject('pdbody')\n"
        << "if _body:\n"
        << "    _sk = _body.newObject('Sketcher::SketchObject', 'Sketch')\n"
        << "    _xy = [f for f in _body.Origin.OriginFeatures if f.Role == 'XY_Plane'][0]\n"
        << "    _sk.AttachmentSupport = [(_xy, '')]\n"
        << "    _sk.MapMode = 'FlatFace'\n"
        << "    _sk.AttachmentOffset = _body.getGlobalPlacement().inverse().multiply(_pl)\n"
        << "else:\n"
        << "    _sk = FreeCAD.ActiveDocument.addObject('Sketcher::SketchObject', 'Sketch')\n"
        << "    _sk.Placement = _pl\n"
        << "FreeCAD.ActiveDocument.recompute()\n"
        << "FreeCADGui.ActiveDocument.setEdit(_sk)\n";

    int tid = Command::openActiveDocumentCommand(
        std::string(QT_TRANSLATE_NOOP("Command", "Create sketch on section plane"))
    );
    try {
        Command::runCommand(Command::Doc, "import FreeCAD, FreeCADGui");
        Command::runCommand(Command::Doc, str.str().c_str());
        Command::commitCommand(tid);
    }
    catch (const Base::Exception& e) {
        Command::abortCommand(tid);
        e.reportException();
    }
}

#include "moc_SectionView.cpp"

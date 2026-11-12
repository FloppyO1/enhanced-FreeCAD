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
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <Inventor/SbBox3f.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoDepthBuffer.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoTextureCoordinate2.h>
#include <Inventor/nodes/SoTransform.h>

#include <QApplication>
#include <QCursor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <App/Application.h>
#include <Base/Color.h>

#include "ViewOrientationCube.h"
#include "NaviCube.h"
#include "Inventor/SoNaviCube.h"
#include "Selection/Selection.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "View3DSettings.h"

using namespace Gui;
using PickId = SoNaviCube::PickId;

namespace
{
constexpr const char* objectNameOfCube = "ViewOrientationCube";
constexpr float faceTransparency = 0.82F;
constexpr float hiliteTransparency = 0.5F;
constexpr int zoneCount = 26;  // PickId::Front .. PickId::RearBottomRight

PickId pickIdOfZone(int zone)
{
    if (zone < 0 || zone >= zoneCount) {
        return PickId::None;
    }
    return static_cast<PickId>(zone + 1);
}

ParameterGrp::handle naviCubeParameters()
{
    return App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/NaviCube"
    );
}

SbColor colorParameter(const char* name, unsigned long def)
{
    Base::Color color(static_cast<uint32_t>(naviCubeParameters()->GetUnsigned(name, def)));
    return {color.r, color.g, color.b};
}

// The colors of the navigation cube: base, emphasis (borders and labels) and hilite.
void naviCubeColors(SbColor& base, SbColor& emphase, SbColor& hilite)
{
    auto hGrp = naviCubeParameters();
    Base::Color bc(static_cast<uint32_t>(hGrp->GetUnsigned("BaseColor", 3806916544UL)));
    base = SbColor(bc.r, bc.g, bc.b);
    const unsigned long defEmphase = bc.r + bc.g + bc.b >= 1.5F ? 255UL : 4294967295UL;
    Base::Color ec(static_cast<uint32_t>(hGrp->GetUnsigned("EmphaseColor", defEmphase)));
    emphase = SbColor(ec.r, ec.g, ec.b);
    Base::Color hc(static_cast<uint32_t>(hGrp->GetUnsigned("HiliteColor", 2867003391UL)));
    hilite = SbColor(hc.r, hc.g, hc.b);
}

// Label texture: the text centered on a transparent square.
void setLabelImage(SoTexture2* texture, const QString& text)
{
    constexpr int size = 256;
    QImage image(size, size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font = QApplication::font();
    font.setBold(true);
    int pixels = 56;
    font.setPixelSize(pixels);
    while (pixels > 12 && QFontMetrics(font).horizontalAdvance(text) > size - 24) {
        pixels -= 4;
        font.setPixelSize(pixels);
    }
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter, text);
    painter.end();

    // Coin images start from the bottom row
    QImage flipped = image.flipped(Qt::Vertical);
    texture->image.setValue(SbVec2s(size, size), 4, flipped.constBits());
}
}  // namespace

/* TRANSLATOR Gui::ViewOrientationCube */

ViewOrientationCube::ViewOrientationCube(View3DInventor* view)
    : QObject(view)
    , view(view)
    , viewer(view->getViewer())
{
    setObjectName(QLatin1String(objectNameOfCube));
    buildSceneNodes();
}

ViewOrientationCube::~ViewOrientationCube()
{
    if (shown) {
        qApp->removeEventFilter(this);
        if (parent) {
            parent->removeChild(root);
            parent->unref();
        }
    }
    root->unref();
}

ViewOrientationCube* ViewOrientationCube::of(View3DInventor* view, bool create)
{
    if (!view) {
        return nullptr;
    }
    auto cube = view->findChild<ViewOrientationCube*>(
        QLatin1String(objectNameOfCube),
        Qt::FindDirectChildrenOnly
    );
    if (!cube && create) {
        cube = new ViewOrientationCube(view);
    }
    return cube;
}

void ViewOrientationCube::toggle(View3DInventor* view)
{
    auto cube = of(view, true);
    if (!cube) {
        return;
    }
    if (cube->isShown()) {
        cube->hide();
    }
    else {
        cube->show();
    }
}

void ViewOrientationCube::buildSceneNodes()
{
    if (!root) {
        root = new SoSeparator();
        root->ref();
        root->setName("ViewOrientationCube");
        root->renderCaching = SoSeparator::OFF;
    }
    else {
        root->removeAllChildren();
    }

    auto hGrp = naviCubeParameters();
    const float chamfer
        = std::clamp(static_cast<float>(hGrp->GetFloat("ChamferSize", 0.12)), 0.05F, 0.18F);
    const float borderWidth = std::max(1.0F, static_cast<float>(hGrp->GetFloat("BorderWidth", 1.1)));
    SbColor baseColor;
    SbColor emphaseColor;
    SbColor hiliteColor;
    naviCubeColors(baseColor, emphaseColor, hiliteColor);

    // the cube is picked by ViewOrientationCube itself, never by the scene
    auto pickStyle = new SoPickStyle();
    pickStyle->style = SoPickStyle::UNPICKABLE;
    root->addChild(pickStyle);

    xform = new SoTransform();
    root->addChild(xform);

    auto lightModel = new SoLightModel();
    lightModel->model = SoLightModel::BASE_COLOR;
    root->addChild(lightModel);

    // the model stays visible: the cube is tested against it but does not hide it
    auto depth = new SoDepthBuffer();
    depth->write = FALSE;
    root->addChild(depth);

    // only the sides facing the camera are drawn
    auto hints = new SoShapeHints();
    hints->vertexOrdering = SoShapeHints::COUNTERCLOCKWISE;
    hints->shapeType = SoShapeHints::SOLID;
    root->addChild(hints);

    // The zones are the faces, edges and corners of the navigation cube, with the same shape.
    zones.clear();
    std::vector<SbVec3f> points;
    std::vector<std::vector<int32_t>> zoneIndices(zoneCount);
    std::vector<int32_t> lineIndices;
    for (int zone = 0; zone < zoneCount; ++zone) {
        std::vector<SbVec3f> polygon = SoNaviCube::cubeFacePolygon(pickIdOfZone(zone), chamfer);
        if (polygon.size() < 3) {
            zones.emplace_back();
            continue;
        }
        // counterclockwise seen from outside
        SbVec3f normal(0, 0, 0);
        SbVec3f centroid(0, 0, 0);
        for (size_t i = 0; i < polygon.size(); ++i) {
            const SbVec3f& p = polygon[i];
            const SbVec3f& q = polygon[(i + 1) % polygon.size()];
            normal += p.cross(q);
            centroid += p;
        }
        if (normal.dot(centroid) < 0.0F) {
            std::reverse(polygon.begin(), polygon.end());
        }
        const auto base = static_cast<int32_t>(points.size());
        for (size_t i = 0; i < polygon.size(); ++i) {
            points.push_back(polygon[i]);
            zoneIndices[zone].push_back(base + static_cast<int32_t>(i));
            lineIndices.push_back(base + static_cast<int32_t>(i));
        }
        zoneIndices[zone].push_back(-1);
        lineIndices.push_back(base);
        lineIndices.push_back(-1);
        zones.push_back(std::move(polygon));
    }

    auto coords = new SoCoordinate3();
    coords->point.setValues(0, static_cast<int>(points.size()), points.data());
    root->addChild(coords);

    // one material per zone, so that the zone under the mouse can be highlighted
    faceMaterials.assign(zoneCount, nullptr);
    for (int zone = 0; zone < zoneCount; ++zone) {
        if (zones[zone].size() < 3) {
            continue;
        }
        auto sep = new SoSeparator();
        auto material = new SoMaterial();
        material->diffuseColor.setValue(baseColor);
        material->transparency.setValue(faceTransparency);
        sep->addChild(material);
        auto faceSet = new SoIndexedFaceSet();
        const auto& idx = zoneIndices[zone];
        faceSet->coordIndex.setValues(0, static_cast<int>(idx.size()), idx.data());
        sep->addChild(faceSet);
        root->addChild(sep);
        faceMaterials[zone] = material;
    }

    // borders of all the zones, like the navigation cube
    auto borders = new SoSeparator();
    auto borderMaterial = new SoMaterial();
    borderMaterial->diffuseColor.setValue(emphaseColor);
    borderMaterial->transparency.setValue(0.35F);
    borders->addChild(borderMaterial);
    auto borderStyle = new SoDrawStyle();
    borderStyle->lineWidth = borderWidth + 0.5F;
    borders->addChild(borderStyle);
    auto borderSet = new SoIndexedLineSet();
    borderSet->coordIndex.setValues(0, static_cast<int>(lineIndices.size()), lineIndices.data());
    borders->addChild(borderSet);
    root->addChild(borders);

    // Labels of the six faces, with the texts and the translations of the navigation cube.
    struct LabelFace
    {
        const char* parameter;
        const char* text;
        SbVec3f normal;
        SbVec3f right;
        SbVec3f up;
    };
    const std::array<LabelFace, 6> labels {{
        {"TextFront", QT_TRANSLATE_NOOP("NaviCubeSettings", "FRONT"), {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {"TextTop", QT_TRANSLATE_NOOP("NaviCubeSettings", "TOP"), {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {"TextRight", QT_TRANSLATE_NOOP("NaviCubeSettings", "RIGHT"), {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {"TextRear", QT_TRANSLATE_NOOP("NaviCubeSettings", "REAR"), {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}},
        {"TextBottom", QT_TRANSLATE_NOOP("NaviCubeSettings", "BOTTOM"), {0, 0, -1}, {1, 0, 0}, {0, -1, 0}},
        {"TextLeft", QT_TRANSLATE_NOOP("NaviCubeSettings", "LEFT"), {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}},
    }};
    const float labelHalf = 1.0F - 2.0F * chamfer;
    for (const auto& label : labels) {
        QByteArray def = NaviCubeSettings::tr(label.text).toUtf8();
        std::string text = hGrp->GetASCII(label.parameter, def.constData());

        auto sep = new SoSeparator();
        auto mat = new SoMaterial();
        mat->diffuseColor.setValue(emphaseColor);
        sep->addChild(mat);
        auto texture = new SoTexture2();
        texture->model = SoTexture2::MODULATE;
        setLabelImage(texture, QString::fromStdString(text));
        sep->addChild(texture);
        auto texCoords = new SoTextureCoordinate2();
        texCoords->point.set1Value(0, SbVec2f(0, 0));
        texCoords->point.set1Value(1, SbVec2f(1, 0));
        texCoords->point.set1Value(2, SbVec2f(1, 1));
        texCoords->point.set1Value(3, SbVec2f(0, 1));
        sep->addChild(texCoords);
        auto quadCoords = new SoCoordinate3();
        const SbVec3f c = label.normal * 1.002F;
        const SbVec3f r = label.right * labelHalf;
        const SbVec3f t = label.up * labelHalf;
        quadCoords->point.set1Value(0, c - r - t);
        quadCoords->point.set1Value(1, c + r - t);
        quadCoords->point.set1Value(2, c + r + t);
        quadCoords->point.set1Value(3, c - r + t);
        sep->addChild(quadCoords);
        auto quad = new SoIndexedFaceSet();
        const std::array<int32_t, 5> quadIndex {0, 1, 2, 3, -1};
        quad->coordIndex.setValues(0, 5, quadIndex.data());
        sep->addChild(quad);
        root->addChild(sep);
    }
    hilite = -1;
}

bool ViewOrientationCube::show()
{
    if (shown || !view) {
        return shown;
    }
    SbBox3f box = viewer->getBoundingBox();
    if (box.isEmpty()) {
        return false;
    }
    // rebuild, so that changes of the navigation cube settings are taken into account
    buildSceneNodes();
    float dx {};
    float dy {};
    float dz {};
    box.getSize(dx, dy, dz);
    half = std::max({dx, dy, dz}) * 0.5F * 1.25F;
    if (half <= 0.0F) {
        half = 1.0F;
    }
    center = box.getCenter();
    xform->translation.setValue(center);
    xform->scaleFactor.setValue(half, half, half);
    setHilite(-1);

    parent = static_cast<SoGroup*>(viewer->getSceneGraph());
    parent->ref();
    parent->addChild(root);
    shown = true;
    leftDown = false;
    dragging = false;

    Selection().rmvPreselect();
    qApp->installEventFilter(this);
    // fit the cube too, so the model is shown a little smaller than a plain fit all
    const SbVec3f extent(half, half, half);
    viewer->viewBoundBox(SbBox3f(center - extent * 1.05F, center + extent * 1.05F));
    return true;
}

void ViewOrientationCube::hide()
{
    if (!shown) {
        return;
    }
    qApp->removeEventFilter(this);
    shown = false;
    leftDown = false;
    dragging = false;
    setHilite(-1);
    if (parent) {
        parent->removeChild(root);
        parent->unref();
        parent = nullptr;
    }
    viewer->redraw();
}

int ViewOrientationCube::pickZone(const SbVec2s& pos) const
{
    if (!shown) {
        return -1;
    }
    SbVec3f p1;
    SbVec3f p2;
    viewer->projectPointToLine(pos, p1, p2);
    // ray in the cube frame, where the cube spans -1..1
    const SbVec3f origin = (p1 - center) / half;
    const SbVec3f dir = (p2 - p1) / half;

    int best = -1;
    float bestT = std::numeric_limits<float>::max();
    for (size_t zone = 0; zone < zones.size(); ++zone) {
        const auto& poly = zones[zone];
        if (poly.size() < 3) {
            continue;
        }
        SbVec3f normal(0, 0, 0);
        for (size_t i = 0; i < poly.size(); ++i) {
            normal += poly[i].cross(poly[(i + 1) % poly.size()]);
        }
        const float denom = normal.dot(dir);
        if (denom >= -1e-12F) {
            continue;  // back face or parallel
        }
        const float t = normal.dot(poly[0] - origin) / denom;
        if (t < 0.0F || t >= bestT) {
            continue;
        }
        const SbVec3f hit = origin + dir * t;
        bool inside = true;
        for (size_t i = 0; i < poly.size() && inside; ++i) {
            const SbVec3f edge = poly[(i + 1) % poly.size()] - poly[i];
            inside = edge.cross(hit - poly[i]).dot(normal) >= 0.0F;
        }
        if (inside) {
            bestT = t;
            best = static_cast<int>(zone);
        }
    }
    return best;
}

PickId ViewOrientationCube::pickAt(const SbVec2s& pos) const
{
    return pickIdOfZone(pickZone(pos));
}

void ViewOrientationCube::setHilite(int zone)
{
    if (zone == hilite) {
        return;
    }
    SbColor base;
    SbColor emphase;
    SbColor hiliteColor;
    naviCubeColors(base, emphase, hiliteColor);
    if (hilite >= 0 && faceMaterials[hilite]) {
        faceMaterials[hilite]->diffuseColor.setValue(base);
        faceMaterials[hilite]->transparency.setValue(faceTransparency);
    }
    hilite = zone;
    if (hilite >= 0 && faceMaterials[hilite]) {
        faceMaterials[hilite]->diffuseColor.setValue(hiliteColor);
        faceMaterials[hilite]->transparency.setValue(hiliteTransparency);
    }
}

void ViewOrientationCube::activate(PickId id)
{
    if (id == PickId::None || !view) {
        return;
    }
    hide();
    const bool nearest = naviCubeParameters()->GetBool("NaviRotateToNearest", true);
    const SbRotation orientation = nearest ? NaviCube::getNearestFaceOrientation(viewer, id)
                                           : NaviCube::getFaceOrientation(id);
    viewer->setCameraOrientation(orientation);
    fitWhenIdle(150);
}

void ViewOrientationCube::fitWhenIdle(int retries)
{
    if (viewer->isAnimating() && retries > 0) {
        QPointer<ViewOrientationCube> guard(this);
        QTimer::singleShot(20, this, [guard, retries]() {
            if (guard && guard->view) {
                guard->fitWhenIdle(retries - 1);
            }
        });
        return;
    }
    viewer->viewAll();
}

bool ViewOrientationCube::isViewerWidget(QObject* obj) const
{
    return view && obj == viewer->getGLWidget();
}

SbVec2s ViewOrientationCube::toCoin(const QPoint& pos) const
{
    QWidget* widget = viewer->getGLWidget();
    const qreal ratio = widget->devicePixelRatioF();
    const int x = static_cast<int>(pos.x() * ratio);
    const int y = static_cast<int>((widget->height() - pos.y()) * ratio) - 1;
    return {static_cast<short>(x), static_cast<short>(y)};
}

bool ViewOrientationCube::eventFilter(QObject* watched, QEvent* event)
{
    if (!shown || replaying || !view) {
        return false;
    }

    switch (event->type()) {
        case QEvent::ShortcutOverride:
        case QEvent::KeyPress: {
            auto key = static_cast<QKeyEvent*>(event);
            QWidget* focus = QApplication::focusWidget();
            QWidget* hovered = QApplication::widgetAt(QCursor::pos());
            const bool inView = (focus && (focus == view || view->isAncestorOf(focus)))
                || (hovered && (hovered == view || view->isAncestorOf(hovered)));
            if (key->key() != Qt::Key_Escape || !inView) {
                return false;
            }
            if (event->type() == QEvent::ShortcutOverride) {
                // deliver Esc to us instead of triggering a shortcut
                event->accept();
                return false;
            }
            hide();
            return true;
        }
        default:
            break;
    }

    if (!isViewerWidget(watched)) {
        return false;
    }

    switch (event->type()) {
        case QEvent::MouseMove: {
            auto me = static_cast<QMouseEvent*>(event);
            if (leftDown && !dragging) {
                if ((me->pos() - pressPos).manhattanLength() < QApplication::startDragDistance()) {
                    return true;
                }
                // a drag: hand the held back press to the navigation
                dragging = true;
                setHilite(-1);
                QMouseEvent press(
                    QEvent::MouseButtonPress,
                    QPointF(pressPos),
                    pressGlobalPos,
                    Qt::LeftButton,
                    Qt::LeftButton,
                    pressModifiers
                );
                replaying = true;
                QCoreApplication::sendEvent(watched, &press);
                replaying = false;
                return false;
            }
            if (me->buttons() != Qt::NoButton) {
                return false;
            }
            // hovering: highlight the cube, never the model
            setHilite(pickZone(toCoin(me->pos())));
            return true;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            auto me = static_cast<QMouseEvent*>(event);
            if (me->button() != Qt::LeftButton) {
                return false;
            }
            if (event->type() == QEvent::MouseButtonPress) {
                leftDown = true;
                dragging = false;
                pressPos = me->pos();
                pressGlobalPos = me->globalPosition();
                pressModifiers = me->modifiers();
            }
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto me = static_cast<QMouseEvent*>(event);
            if (me->button() != Qt::LeftButton || !leftDown) {
                return false;
            }
            leftDown = false;
            if (dragging) {
                dragging = false;
                return false;
            }
            const PickId id = pickAt(toCoin(me->pos()));
            if (id != PickId::None) {
                activate(id);
            }
            return true;
        }
        case QEvent::Leave:
            setHilite(-1);
            return false;
        default:
            return false;
    }
}

#include "moc_ViewOrientationCube.cpp"

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
#include "Selection/Selection.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "View3DSettings.h"

using namespace Gui;
using PickId = SoNaviCube::PickId;

namespace
{
constexpr const char* objectNameOfCube = "ViewOrientationCube";
// half width of the central (face) part of each cube side, the cube spans -1..1
constexpr float faceBand = 0.6F;
constexpr float faceTransparency = 0.88F;
constexpr float hiliteTransparency = 0.55F;
constexpr int regionCount = 27;

int regionIndex(int sx, int sy, int sz)
{
    return (sx + 1) * 9 + (sy + 1) * 3 + (sz + 1);
}

// The same face, edge and corner naming as the navigation cube:
// front is -Y, right is +X, top is +Z.
PickId pickIdOfRegion(int region)
{
    if (region < 0 || region >= regionCount) {
        return PickId::None;
    }
    const int sx = region / 9 - 1;
    const int sy = (region / 3) % 3 - 1;
    const int sz = region % 3 - 1;
    const bool front = sy < 0;
    switch (std::abs(sx) + std::abs(sy) + std::abs(sz)) {
        case 1:
            if (sx != 0) {
                return sx > 0 ? PickId::Right : PickId::Left;
            }
            if (sy != 0) {
                return front ? PickId::Front : PickId::Rear;
            }
            return sz > 0 ? PickId::Top : PickId::Bottom;
        case 2:
            if (sy != 0 && sz != 0) {
                return front ? (sz > 0 ? PickId::FrontTop : PickId::FrontBottom)
                             : (sz > 0 ? PickId::RearTop : PickId::RearBottom);
            }
            if (sy != 0) {
                return front ? (sx > 0 ? PickId::FrontRight : PickId::FrontLeft)
                             : (sx > 0 ? PickId::RearRight : PickId::RearLeft);
            }
            return sz > 0 ? (sx > 0 ? PickId::TopRight : PickId::TopLeft)
                          : (sx > 0 ? PickId::BottomRight : PickId::BottomLeft);
        case 3:
            if (front) {
                return sz > 0 ? (sx > 0 ? PickId::FrontTopRight : PickId::FrontTopLeft)
                              : (sx > 0 ? PickId::FrontBottomRight : PickId::FrontBottomLeft);
            }
            return sz > 0 ? (sx > 0 ? PickId::RearTopRight : PickId::RearTopLeft)
                          : (sx > 0 ? PickId::RearBottomRight : PickId::RearBottomLeft);
        default:
            return PickId::None;
    }
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
    painter.setPen(QColor(30, 30, 30, 150));
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
    root = new SoSeparator();
    root->ref();
    root->setName("ViewOrientationCube");
    root->renderCaching = SoSeparator::OFF;

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

    // Each side is split in 3x3 parts: the middle one is the face, the outer ones
    // belong to the edges and corners shared with the next sides.
    const std::array<float, 4> grid {-1.0F, -faceBand, faceBand, 1.0F};
    std::vector<SbVec3f> points;
    std::vector<int32_t> faceIndices;
    std::vector<int32_t> materialIndices;
    std::vector<SbVec3f> linePoints;
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        for (int side : {-1, 1}) {
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    std::array<SbVec3f, 4> quad;
                    const std::array<std::array<int, 2>, 4> corners {
                        {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}}
                    };
                    for (size_t k = 0; k < 4; ++k) {
                        SbVec3f p;
                        p[axis] = static_cast<float>(side);
                        p[u] = grid[corners[k][0]];
                        p[v] = grid[corners[k][1]];
                        quad[k] = p;
                    }
                    if (side < 0) {
                        std::swap(quad[1], quad[3]);
                    }
                    const auto base = static_cast<int32_t>(points.size());
                    for (const auto& p : quad) {
                        points.push_back(p);
                    }
                    faceIndices.insert(faceIndices.end(), {base, base + 1, base + 2, base + 3, -1});
                    std::array<int, 3> signs {};
                    signs[axis] = side;
                    signs[u] = i - 1;
                    signs[v] = j - 1;
                    materialIndices.push_back(regionIndex(signs[0], signs[1], signs[2]));
                }
            }
            // thin lines between the face and its edges and corners
            for (float value : {-faceBand, faceBand}) {
                SbVec3f a;
                SbVec3f b;
                a[axis] = b[axis] = static_cast<float>(side);
                a[u] = -1.0F;
                b[u] = 1.0F;
                a[v] = b[v] = value;
                linePoints.push_back(a);
                linePoints.push_back(b);
                a[v] = -1.0F;
                b[v] = 1.0F;
                a[u] = b[u] = value;
                linePoints.push_back(a);
                linePoints.push_back(b);
            }
        }
    }

    auto faces = new SoSeparator();
    faceMaterial = new SoMaterial();
    SbColor base = colorParameter("BaseColor", 3806916544UL);
    faceMaterial->diffuseColor.setNum(regionCount);
    faceMaterial->transparency.setNum(regionCount);
    for (int k = 0; k < regionCount; ++k) {
        faceMaterial->diffuseColor.set1Value(k, base);
        faceMaterial->transparency.set1Value(k, faceTransparency);
    }
    faces->addChild(faceMaterial);
    auto binding = new SoMaterialBinding();
    binding->value = SoMaterialBinding::PER_FACE_INDEXED;
    faces->addChild(binding);
    auto coords = new SoCoordinate3();
    coords->point.setValues(0, static_cast<int>(points.size()), points.data());
    faces->addChild(coords);
    auto faceSet = new SoIndexedFaceSet();
    faceSet->coordIndex.setValues(0, static_cast<int>(faceIndices.size()), faceIndices.data());
    faceSet->materialIndex.setValues(
        0,
        static_cast<int>(materialIndices.size()),
        materialIndices.data()
    );
    faces->addChild(faceSet);
    root->addChild(faces);

    // cube outline and the borders of the faces
    std::vector<SbVec3f> edgePoints;
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        for (int su : {-1, 1}) {
            for (int sv : {-1, 1}) {
                SbVec3f a;
                SbVec3f b;
                a[axis] = -1.0F;
                b[axis] = 1.0F;
                a[u] = b[u] = static_cast<float>(su);
                a[v] = b[v] = static_cast<float>(sv);
                edgePoints.push_back(a);
                edgePoints.push_back(b);
            }
        }
    }
    auto addLines = [this](const std::vector<SbVec3f>& pts, float width, float gray, float alpha) {
        auto sep = new SoSeparator();
        auto mat = new SoMaterial();
        mat->diffuseColor.setValue(gray, gray, gray);
        mat->transparency.setValue(alpha);
        sep->addChild(mat);
        auto style = new SoDrawStyle();
        style->lineWidth = width;
        sep->addChild(style);
        auto lineCoords = new SoCoordinate3();
        lineCoords->point.setValues(0, static_cast<int>(pts.size()), pts.data());
        sep->addChild(lineCoords);
        auto lines = new SoIndexedLineSet();
        std::vector<int32_t> idx;
        for (int32_t k = 0; k + 1 < static_cast<int32_t>(pts.size()); k += 2) {
            idx.insert(idx.end(), {k, k + 1, -1});
        }
        lines->coordIndex.setValues(0, static_cast<int>(idx.size()), idx.data());
        sep->addChild(lines);
        root->addChild(sep);
    };
    addLines(edgePoints, 2.0F, 0.25F, 0.2F);
    addLines(linePoints, 1.0F, 0.35F, 0.6F);

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
    auto hGrp = naviCubeParameters();
    for (const auto& label : labels) {
        QByteArray def = NaviCubeSettings::tr(label.text).toUtf8();
        std::string text = hGrp->GetASCII(label.parameter, def.constData());

        auto sep = new SoSeparator();
        auto mat = new SoMaterial();
        mat->diffuseColor.setValue(1.0F, 1.0F, 1.0F);
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
        const SbVec3f r = label.right * faceBand;
        const SbVec3f t = label.up * faceBand;
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

int ViewOrientationCube::pickRegion(const SbVec2s& pos) const
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

    float tmin = -std::numeric_limits<float>::max();
    float tmax = std::numeric_limits<float>::max();
    int entryAxis = -1;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(dir[k]) < 1e-12F) {
            if (std::fabs(origin[k]) > 1.0F) {
                return -1;
            }
            continue;
        }
        float t1 = (-1.0F - origin[k]) / dir[k];
        float t2 = (1.0F - origin[k]) / dir[k];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        if (t1 > tmin) {
            tmin = t1;
            entryAxis = k;
        }
        tmax = std::min(tmax, t2);
    }
    if (entryAxis < 0 || tmin > tmax || tmin < 0.0F) {
        return -1;
    }
    const SbVec3f hit = origin + dir * tmin;
    std::array<int, 3> signs {};
    for (int k = 0; k < 3; ++k) {
        if (k == entryAxis) {
            signs[k] = hit[k] > 0.0F ? 1 : -1;
        }
        else if (std::fabs(hit[k]) > faceBand) {
            signs[k] = hit[k] > 0.0F ? 1 : -1;
        }
    }
    return regionIndex(signs[0], signs[1], signs[2]);
}

PickId ViewOrientationCube::pickAt(const SbVec2s& pos) const
{
    return pickIdOfRegion(pickRegion(pos));
}

void ViewOrientationCube::setHilite(int region)
{
    if (region == hilite) {
        return;
    }
    SbColor base = colorParameter("BaseColor", 3806916544UL);
    if (hilite >= 0) {
        faceMaterial->diffuseColor.set1Value(hilite, base);
        faceMaterial->transparency.set1Value(hilite, faceTransparency);
    }
    hilite = region;
    if (hilite >= 0) {
        faceMaterial->diffuseColor.set1Value(hilite, colorParameter("HiliteColor", 2867003391UL));
        faceMaterial->transparency.set1Value(hilite, hiliteTransparency);
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
            if (key->key() != Qt::Key_Escape || !focus
                || (focus != view && !view->isAncestorOf(focus))) {
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
            setHilite(pickRegion(toCoin(me->pos())));
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

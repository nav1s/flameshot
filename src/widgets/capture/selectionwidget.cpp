// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "selectionwidget.h"
#include "utils/globalvalues.h"
#include "widgets/capture/capturetoolbutton.h"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>
#include <utility>

#define MARGIN (m_THandle.width())

SelectionWidget::SelectionWidget(QColor c, QWidget* parent)
  : QWidget(parent)
  , m_color(std::move(c))
  , m_shortcut_type(NO_TYPE)
  , m_activeSide(NO_SIDE)
  , m_repeatCount(0)
  , m_ignoreMouse(false)
  , m_aspectRatio(1)
{
    // prevents this widget from consuming CaptureToolButton mouse events
    setAttribute(Qt::WA_TransparentForMouseEvents);
    parent->installEventFilter(this);

    m_animation = new QPropertyAnimation(this, "geometry", this);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);
    m_animation->setDuration(200);
    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
        emit geometrySettled();
    });

    int sideVal = GlobalValues::buttonBaseSize() * 0.6;
    int handleSide = sideVal / 2;
    const QRect areaRect(0, 0, sideVal, sideVal);

    const QRect handleRect(0, 0, handleSide, handleSide);
    m_TLHandle = m_TRHandle = m_BLHandle = m_BRHandle = m_LHandle = m_THandle =
      m_RHandle = m_BHandle = handleRect;
    m_TLArea = m_TRArea = m_BLArea = m_BRArea = areaRect;

    m_areaOffset = QPoint(-sideVal / 2, -sideVal / 2);
    m_handleOffset = QPoint(-handleSide / 2, -handleSide / 2);
    connect(this, &SelectionWidget::geometrySettled, this, [this]() {
        m_repeatCount = 0;
        m_shortcut_type = NO_TYPE;
    });
}

/**
 * @brief Get the side where the mouse cursor is.
 * @param mousePos Mouse cursor position relative to the parent widget.
 */
SelectionWidget::SideType SelectionWidget::getMouseSide(
  const QPoint& mousePos) const
{
    if (!isVisible()) {
        return NO_SIDE;
    }
    QPoint localPos = mapFromParent(mousePos);
    if (m_TLArea.contains(localPos)) {
        return TOPLEFT_SIDE;
    } else if (m_TRArea.contains(localPos)) {
        return TOPRIGHT_SIDE;
    } else if (m_BLArea.contains(localPos)) {
        return BOTTOMLEFT_SIDE;
    } else if (m_BRArea.contains(localPos)) {
        return BOTTOMRIGHT_SIDE;
    } else if (m_LArea.contains(localPos)) {
        return LEFT_SIDE;
    } else if (m_TArea.contains(localPos)) {
        return TOP_SIDE;
    } else if (m_RArea.contains(localPos)) {
        return RIGHT_SIDE;
    } else if (m_BArea.contains(localPos)) {
        return BOTTOM_SIDE;
    } else if (rect().contains(localPos)) {
        return CENTER;
    } else {
        return NO_SIDE;
    }
}

QVector<QRect> SelectionWidget::handlerAreas()
{
    QVector<QRect> areas;
    areas << m_TLHandle << m_TRHandle << m_BLHandle << m_BRHandle << m_LHandle
          << m_THandle << m_RHandle << m_BHandle;
    return areas;
}

// helper function
SelectionWidget::SideType getProperSide(SelectionWidget::SideType side,
                                        const QRect& r)
{
    using SideType = SelectionWidget::SideType;
    int intSide = side;
    if (r.right() < r.left()) {
        intSide ^= SideType::LEFT_SIDE;
        intSide ^= SideType::RIGHT_SIDE;
    }
    if (r.bottom() < r.top()) {
        intSide ^= SideType::TOP_SIDE;
        intSide ^= SideType::BOTTOM_SIDE;
    }

    return (SideType)intSide;
}

void SelectionWidget::setIgnoreMouse(bool ignore)
{
    m_ignoreMouse = ignore;
    updateCursor();
}

/**
 * Set the cursor that will be active when the mouse is inside the selection and
 * the mouse is not clicked.
 */
void SelectionWidget::setIdleCentralCursor(const QCursor& cursor)
{
    m_idleCentralCursor = cursor;
}

void SelectionWidget::setGeometryAnimated(const QRect& r)
{
    if (isVisible()) {
        m_animation->setStartValue(geometry());
        m_animation->setEndValue(r);
        m_animation->start();
    }
}

void SelectionWidget::setGeometry(const QRect& r)
{
    QWidget::setGeometry(r + QMargins(MARGIN, MARGIN, MARGIN, MARGIN));
    updateCursor();
    if (isVisible()) {
        emit geometryChanged();
    }
}

QRect SelectionWidget::geometry() const
{
    return QWidget::geometry() - QMargins(MARGIN, MARGIN, MARGIN, MARGIN);
}

QRect SelectionWidget::fullGeometry() const
{
    return QWidget::geometry();
}

QRect SelectionWidget::rect() const
{
    return QWidget::rect() - QMargins(MARGIN, MARGIN, MARGIN, MARGIN);
}

bool SelectionWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (m_ignoreMouse && dynamic_cast<QMouseEvent*>(event)) {
        m_activeSide = NO_SIDE;
        unsetCursor();
    } else if (event->type() == QEvent::MouseButtonRelease) {
        parentMouseReleaseEvent(static_cast<QMouseEvent*>(event));
    } else if (event->type() == QEvent::MouseButtonPress) {
        parentMousePressEvent(static_cast<QMouseEvent*>(event));
    } else if (event->type() == QEvent::MouseMove) {
        parentMouseMoveEvent(static_cast<QMouseEvent*>(event));
    }
    return false;
}

void SelectionWidget::parentMousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) {
        return;
    }

    m_dragStartPos = e->pos();
    m_activeSide = getMouseSide(e->pos());
    if ((float)geometry().height() > 0) {
        m_aspectRatio = (float)geometry().width() / (float)geometry().height();
    }
}

void SelectionWidget::parentMouseReleaseEvent(QMouseEvent* e)
{
    // released outside of the selection area
    if (!getMouseSide(e->pos())) {
        hide();
    }

    m_activeSide = NO_SIDE;
    updateCursor();
    emit geometrySettled();
}

void SelectionWidget::parentMouseMoveEvent(QMouseEvent* e)
{
    updateCursor();

    if (e->buttons() != Qt::LeftButton) {
        return;
    }

    SideType mouseSide = m_activeSide;
    if (!m_activeSide) {
        mouseSide = getMouseSide(e->pos());
    }

    QPoint pos;

    if (!isVisible() || !mouseSide) {
        show();
        m_activeSide = TOPLEFT_SIDE;
        pos = m_dragStartPos;
        setGeometry({ pos, pos });
    } else {
        pos = e->pos();
    }

    auto geom = geometry();
    bool symmetryMod = qApp->keyboardModifiers() & Qt::ShiftModifier;
    bool preserveAspect = qApp->keyboardModifiers() & Qt::ControlModifier;

    QPoint newTopLeft = geom.topLeft(), newBottomRight = geom.bottomRight();
    int oldLeft = newTopLeft.rx(), oldRight = newBottomRight.rx(),
        oldTop = newTopLeft.ry(), oldBottom = newBottomRight.ry();
    int &newLeft = newTopLeft.rx(), &newRight = newBottomRight.rx(),
        &newTop = newTopLeft.ry(), &newBottom = newBottomRight.ry();
    switch (mouseSide) {
        case TOPLEFT_SIDE:
            if (m_activeSide) {
                if (preserveAspect) {
                    if ((float)(oldRight - pos.x()) /
                          (float)(oldBottom - pos.y()) >
                        m_aspectRatio) {
                        /* width longer than expected width, hence increase
                         * height to compensate for the aspect ratio */
                        newLeft = pos.x();
                        newTop =
                          oldBottom -
                          (int)(((float)(oldRight - pos.x())) / m_aspectRatio);
                    } else {
                        /* height longer than expected height, hence increase
                         * width to compensate for the aspect ratio */
                        newTop = pos.y();
                        newLeft =
                          oldRight -
                          (int)(((float)(oldBottom - pos.y())) * m_aspectRatio);
                    }
                } else {
                    newTopLeft = pos;
                }
            }
            break;
        case BOTTOMRIGHT_SIDE:
            if (m_activeSide) {
                if (preserveAspect) {
                    if ((float)(pos.x() - oldLeft) / (float)(pos.y() - oldTop) >
                        m_aspectRatio) {
                        newRight = pos.x();
                        newBottom =
                          oldTop +
                          (int)(((float)(pos.x() - oldLeft)) / m_aspectRatio);
                    } else {
                        newBottom = pos.y();
                        newRight = oldLeft + (int)(((float)(pos.y() - oldTop)) *
                                                   m_aspectRatio);
                    }
                } else {
                    newBottomRight = pos;
                }
            }
            break;
        case TOPRIGHT_SIDE:
            if (m_activeSide) {
                if (preserveAspect) {
                    if ((float)(pos.x() - oldLeft) /
                          (float)(oldBottom - pos.y()) >
                        m_aspectRatio) {
                        newRight = pos.x();
                        newTop =
                          oldBottom -
                          (int)(((float)(pos.x() - oldLeft)) / m_aspectRatio);
                    } else {
                        newTop = pos.y();
                        newRight =
                          oldLeft +
                          (int)(((float)(oldBottom - pos.y())) * m_aspectRatio);
                    }
                } else {
                    newTop = pos.y();
                    newRight = pos.x();
                }
            }
            break;
        case BOTTOMLEFT_SIDE:
            if (m_activeSide) {
                if (preserveAspect) {
                    if ((float)(oldRight - pos.x()) /
                          (float)(pos.y() - oldTop) >
                        m_aspectRatio) {
                        newLeft = pos.x();
                        newBottom =
                          oldTop +
                          (int)(((float)(oldRight - pos.x())) / m_aspectRatio);
                    } else {
                        newBottom = pos.y();
                        newLeft = oldRight - (int)(((float)(pos.y() - oldTop)) *
                                                   m_aspectRatio);
                    }
                } else {
                    newBottom = pos.y();
                    newLeft = pos.x();
                }
            }
            break;
        case LEFT_SIDE:
            if (m_activeSide) {
                newLeft = pos.x();
                if (preserveAspect) {
                    /* By default bottom edge moves when dragging sides, this
                     * behavior feels natural */
                    newBottom = oldTop + (int)(((float)(oldRight - pos.x())) /
                                               m_aspectRatio);
                }
            }
            break;
        case RIGHT_SIDE:
            if (m_activeSide) {
                newRight = pos.x();
                if (preserveAspect) {
                    newBottom = oldTop + (int)(((float)(pos.x() - oldLeft)) /
                                               m_aspectRatio);
                }
            }
            break;
        case TOP_SIDE:
            if (m_activeSide) {
                newTop = pos.y();
                if (preserveAspect) {
                    /* By default right edge moves when dragging sides, this
                     * behavior feels natural */
                    newRight =
                      oldLeft +
                      (int)(((float)(oldBottom - pos.y()) * m_aspectRatio));
                }
            }
            break;
        case BOTTOM_SIDE:
            if (m_activeSide) {
                newBottom = pos.y();
                if (preserveAspect) {
                    newRight =
                      oldLeft +
                      (int)(((float)(pos.y() - oldTop) * m_aspectRatio));
                }
            }
            break;
        default:
            if (m_activeSide) {
                move(this->pos() + pos - m_dragStartPos);
                m_dragStartPos = pos;
                /* do nothing special in case of preserveAspect */
            }
            return;
    }
    // finalize geometry change
    if (m_activeSide) {
        if (symmetryMod) {
            QPoint deltaTopLeft = newTopLeft - geom.topLeft();
            QPoint deltaBottomRight = newBottomRight - geom.bottomRight();
            newTopLeft = geom.topLeft() + deltaTopLeft - deltaBottomRight;
            newBottomRight =
              geom.bottomRight() + deltaBottomRight - deltaTopLeft;
        }
        geom = { newTopLeft, newBottomRight };
        setGeometry(geom.normalized());
        m_activeSide = getProperSide(m_activeSide, geom);
    }
    m_dragStartPos = e->pos();
}

void SelectionWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (!p.isActive()) {
        return;
    }
    p.setPen(m_color);
    p.drawRect(rect() + QMargins(0, 0, -1, -1));
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(m_color);
    for (auto rectangle : handlerAreas()) {
        p.drawEllipse(rectangle);
    }
}

void SelectionWidget::resizeEvent(QResizeEvent*)
{
    updateAreas();
    if (isVisible()) {
        emit geometryChanged();
    }
}

void SelectionWidget::moveEvent(QMoveEvent*)
{
    updateAreas();
    if (isVisible()) {
        emit geometryChanged();
    }
}

void SelectionWidget::showEvent(QShowEvent*)
{
    emit visibilityChanged();
}

void SelectionWidget::hideEvent(QHideEvent*)
{
    emit visibilityChanged();
}

void SelectionWidget::updateColor(const QColor& c)
{
    m_color = c;
}

int SelectionWidget::acceleration(ShortCutType type)
{
    const float a = 0.05;
    const float v0 = 1;
    const float vmax = 30;
    int tmp;
    if (type != NO_TYPE && type != m_shortcut_type) {
        m_shortcut_type = type;
        m_repeatCount = 0;
    }
    tmp = m_repeatCount;
    if (tmp * tmp < (vmax - v0) / a) {
        m_repeatCount++;
        return (int)(a * tmp * tmp + v0);
    } else
        return vmax;
}

void SelectionWidget::moveLeft()
{
    int px = acceleration(MOVE_LEFT_TYPE);
    setGeometryByKeyboard(geometry().adjusted(-px, 0, -px, 0), NO_SIDE);
}

void SelectionWidget::moveRight()
{
    int px = acceleration(MOVE_RIGHT_TYPE);
    setGeometryByKeyboard(geometry().adjusted(px, 0, px, 0), NO_SIDE);
}

void SelectionWidget::moveUp()
{
    int px = acceleration(MOVE_UP_TYPE);
    setGeometryByKeyboard(geometry().adjusted(0, -px, 0, -px), NO_SIDE);
}

void SelectionWidget::moveDown()
{
    int px = acceleration(MOVE_DOWN_TYPE);
    setGeometryByKeyboard(geometry().adjusted(0, px, 0, px), NO_SIDE);
}

void SelectionWidget::resizeLeft()
{
    int px = acceleration(RESIZE_LEFT_TYPE);
    setGeometryByKeyboard(geometry().adjusted(0, 0, -px, 0), RIGHT_SIDE);
}

void SelectionWidget::resizeRight()
{
    int px = acceleration(RESIZE_RIGHT_TYPE);
    setGeometryByKeyboard(geometry().adjusted(0, 0, px, 0), RIGHT_SIDE);
}

void SelectionWidget::resizeUp()
{
    int px = acceleration(RESIZE_UP_TYPE);
    setGeometryByKeyboard(geometry().adjusted(0, 0, 0, -px), BOTTOM_SIDE);
}

void SelectionWidget::resizeDown()
{
    int px = acceleration(RESIZE_DOWN_TYPE);
    setGeometryByKeyboard(geometry().adjusted(0, 0, 0, px), BOTTOM_SIDE);
}

void SelectionWidget::symResizeLeft()
{
    int px = acceleration(SYM_RESIZE_LEFT_TYPE);
    setGeometryByKeyboard(geometry().adjusted(px, 0, -px, 0), CENTER);
}

void SelectionWidget::symResizeRight()
{
    int px = acceleration(SYM_RESIZE_RIGHT_TYPE);
    setGeometryByKeyboard(geometry().adjusted(-px, 0, px, 0), CENTER);
}

void SelectionWidget::symResizeUp()
{
    int px = acceleration(SYM_RESIZE_UP_TYPE);
    setGeometryByKeyboard(geometry().adjusted(0, -px, 0, px), CENTER);
}

void SelectionWidget::symResizeDown()
{
    int px = acceleration(SYM_RESIZE_DOWN_TYPE);
    setGeometryByKeyboard(geometry().adjusted(0, px, 0, -px), CENTER);
}

void SelectionWidget::updateAreas()
{
    QRect r = rect();
    m_TLArea.moveTo(r.topLeft() + m_areaOffset);
    m_TRArea.moveTo(r.topRight() + m_areaOffset);
    m_BLArea.moveTo(r.bottomLeft() + m_areaOffset);
    m_BRArea.moveTo(r.bottomRight() + m_areaOffset);

    m_LArea = QRect(m_TLArea.bottomLeft(), m_BLArea.topRight());
    m_TArea = QRect(m_TLArea.topRight(), m_TRArea.bottomLeft());
    m_RArea = QRect(m_TRArea.bottomLeft(), m_BRArea.topRight());
    m_BArea = QRect(m_BLArea.topRight(), m_BRArea.bottomLeft());

    m_TLHandle.moveTo(m_TLArea.center() + m_handleOffset);
    m_BLHandle.moveTo(m_BLArea.center() + m_handleOffset);
    m_TRHandle.moveTo(m_TRArea.center() + m_handleOffset);
    m_BRHandle.moveTo(m_BRArea.center() + m_handleOffset);
    m_LHandle.moveTo(m_LArea.center() + m_handleOffset);
    m_THandle.moveTo(m_TArea.center() + m_handleOffset);
    m_RHandle.moveTo(m_RArea.center() + m_handleOffset);
    m_BHandle.moveTo(m_BArea.center() + m_handleOffset);
}

void SelectionWidget::updateCursor()
{
    SideType mouseSide = m_activeSide;
    if (!m_activeSide) {
        mouseSide = getMouseSide(parentWidget()->mapFromGlobal(QCursor::pos()));
    }

    switch (mouseSide) {
        case TOPLEFT_SIDE:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case BOTTOMRIGHT_SIDE:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case TOPRIGHT_SIDE:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case BOTTOMLEFT_SIDE:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case LEFT_SIDE:
            setCursor(Qt::SizeHorCursor);
            break;
        case RIGHT_SIDE:
            setCursor(Qt::SizeHorCursor);
            break;
        case TOP_SIDE:
            setCursor(Qt::SizeVerCursor);
            break;
        case BOTTOM_SIDE:
            setCursor(Qt::SizeVerCursor);
            break;
        default:
            if (m_activeSide) {
                setCursor(Qt::ClosedHandCursor);
            } else {
                setCursor(m_idleCentralCursor);
                return;
            }
            break;
    }
}

void SelectionWidget::setGeometryByKeyboard(const QRect& r,
                                            const SideType change_side)
{
    static QTimer timer;
    QRect rectangle = r;
    int pmin_x, pmax_x, pmin_y, pmax_y;
    int rmin_x, rmax_x, rmin_y, rmax_y;

    parentWidget()->rect().getCoords(&pmin_x, &pmin_y, &pmax_x, &pmax_y);
    rectangle.getCoords(&rmin_x, &rmin_y, &rmax_x, &rmax_y);

    // side combinations are not implemented so no need to check for them
    if (rectangle.isEmpty()) {
        switch (change_side) {
            case RIGHT_SIDE:
                rmax_x = rmin_x;
                break;
            case LEFT_SIDE:
                rmin_x = rmax_x;
                break;
            case BOTTOM_SIDE:
                rmax_y = rmin_y;
                break;
            case TOP_SIDE:
                rmin_y = rmax_y;
                break;
            case CENTER:
                QPoint center = rectangle.center();
                if (rmax_x < rmin_x) {
                    rmax_x = rmin_x = center.rx();
                } else {
                    rmax_y = rmin_y = center.ry();
                }
        }
    }
    rmin_x = qBound(pmin_x, rmin_x, pmax_x);
    rmax_x = qBound(pmin_x, rmax_x, pmax_x);
    rmin_y = qBound(pmin_y, rmin_y, pmax_y);
    rmax_y = qBound(pmin_y, rmax_y, pmax_y);
    rectangle.setCoords(rmin_x, rmin_y, rmax_x, rmax_y);

    setGeometry(rectangle);
    connect(&timer,
            &QTimer::timeout,
            this,
            &SelectionWidget::geometrySettled,
            Qt::UniqueConnection);
    timer.start(400);
}

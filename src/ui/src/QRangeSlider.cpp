#include "QRangeSlider.hpp"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QStylePainter>
#include <QWheelEvent>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

QRangeSlider::QRangeSlider(QWidget* parent)
    : QRangeSlider(Qt::Horizontal, parent)
{
}

QRangeSlider::QRangeSlider(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);

    if (m_orientation == Qt::Horizontal)
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    else
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

QRangeSlider::~QRangeSlider() = default;

// ---------------------------------------------------------------------------
// Properties — getters
// ---------------------------------------------------------------------------

int QRangeSlider::minimum() const { return m_minimum; }
int QRangeSlider::maximum() const { return m_maximum; }
int QRangeSlider::lowerValue() const { return m_lowerValue; }
int QRangeSlider::upperValue() const { return m_upperValue; }
QPair<int, int> QRangeSlider::values() const { return { m_lowerValue, m_upperValue }; }
Qt::Orientation QRangeSlider::orientation() const { return m_orientation; }
int QRangeSlider::singleStep() const { return m_singleStep; }

// ---------------------------------------------------------------------------
// Properties — setters
// ---------------------------------------------------------------------------

void QRangeSlider::setMinimum(int min)
{
    const int newMax = qMax(min, m_maximum);
    setRange(min, newMax);
}

void QRangeSlider::setMaximum(int max)
{
    const int newMin = qMin(max, m_minimum);
    setRange(newMin, max);
}

void QRangeSlider::setRange(int min, int max)
{
    min = qMin(min, max);
    max = qMax(min, max);

    if (min == m_minimum && max == m_maximum)
        return;

    m_minimum = min;
    m_maximum = max;

    int lower = qBound(m_minimum, m_lowerValue, m_maximum);
    int upper = qBound(m_minimum, m_upperValue, m_maximum);
    if (lower > upper) {
        lower = m_minimum;
        upper = m_maximum;
    }

    Q_EMIT rangeChanged(m_minimum, m_maximum);

    if (lower != m_lowerValue) {
        m_lowerValue = lower;
        Q_EMIT lowerValueChanged(lower);
    }
    if (upper != m_upperValue) {
        m_upperValue = upper;
        Q_EMIT upperValueChanged(upper);
    }

    update();
    updateGeometry();
}

void QRangeSlider::setLowerValue(int value)
{
    value = qBound(m_minimum, value, m_upperValue);
    if (value == m_lowerValue)
        return;
    m_lowerValue = value;
    update();
    Q_EMIT lowerValueChanged(value);
}

void QRangeSlider::setUpperValue(int value)
{
    value = qBound(m_lowerValue, value, m_maximum);
    if (value == m_upperValue)
        return;
    m_upperValue = value;
    update();
    Q_EMIT upperValueChanged(value);
}

void QRangeSlider::setValues(int lower, int upper)
{
    lower = qBound(m_minimum, lower, m_maximum);
    upper = qBound(m_minimum, upper, m_maximum);
    if (lower > upper)
        std::swap(lower, upper);

    bool changed = false;
    if (lower != m_lowerValue) {
        m_lowerValue = lower;
        Q_EMIT lowerValueChanged(lower);
        changed = true;
    }
    if (upper != m_upperValue) {
        m_upperValue = upper;
        Q_EMIT upperValueChanged(upper);
        changed = true;
    }
    if (changed)
        update();
}

void QRangeSlider::setOrientation(Qt::Orientation orientation)
{
    if (m_orientation == orientation)
        return;
    m_orientation = orientation;

    if (orientation == Qt::Horizontal)
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    else
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    update();
    updateGeometry();
    Q_EMIT orientationChanged(orientation);
}

void QRangeSlider::setSingleStep(int step)
{
    step = qMax(step, 1);
    if (step == m_singleStep)
        return;
    m_singleStep = step;
    update();
}

// ---------------------------------------------------------------------------
// Size hints
// ---------------------------------------------------------------------------

QSize QRangeSlider::sizeHint() const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt, LowerHandle);

    const QRect hr = style()->subControlRect(
        QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    const int thickness = (m_orientation == Qt::Horizontal)
        ? hr.height()
        : hr.width();
    const int length = qMax(120, thickness * 4);

    QSize hint = (m_orientation == Qt::Horizontal)
        ? QSize(length, thickness)
        : QSize(thickness, length);

    return style()->sizeFromContents(QStyle::CT_Slider, &opt, hint, this);
}

QSize QRangeSlider::minimumSizeHint() const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt, LowerHandle);

    const QRect hr = style()->subControlRect(
        QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    const QSize sz = (m_orientation == Qt::Horizontal)
        ? QSize(hr.width() * 4, hr.height())
        : QSize(hr.width(), hr.height() * 4);

    return style()->sizeFromContents(QStyle::CT_Slider, &opt, sz, this);
}

// ---------------------------------------------------------------------------
// Style option helper
// ---------------------------------------------------------------------------

void QRangeSlider::initStyleOption(QStyleOptionSlider* option, Handle handle) const
{
    if (!option)
        [[unlikely]]
        return;

    option->initFrom(this);
    option->subControls = QStyle::SC_None;
    option->activeSubControls = QStyle::SC_None;
    option->orientation = m_orientation;
    option->minimum = m_minimum;
    option->maximum = m_maximum;
    option->singleStep = m_singleStep;
    option->pageStep = qMax(1, (m_maximum - m_minimum) / 10);
    option->sliderPosition = (handle == UpperHandle) ? m_upperValue : m_lowerValue;
    option->sliderValue = option->sliderPosition;
    option->upsideDown = (m_orientation == Qt::Vertical);
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

QRect QRangeSlider::grooveRect() const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt, LowerHandle);
    return style()->subControlRect(
        QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
}

QRect QRangeSlider::handleRect(Handle handle) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt, handle);
    return style()->subControlRect(
        QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
}

QRangeSlider::Handle QRangeSlider::handleAt(const QPoint& pos) const
{
    // Upper handle is drawn last (on top) — test it first so that the
    // visually-frontmost handle wins when the two overlap.
    if (handleRect(UpperHandle).contains(pos))
        return UpperHandle;
    if (handleRect(LowerHandle).contains(pos))
        return LowerHandle;
    return NoHandle;
}

int QRangeSlider::pixelPosToRangeValue(int pos) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt, LowerHandle);

    const QRect gr = style()->subControlRect(
        QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect hr = style()->subControlRect(
        QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

    int minPos, span;
    if (m_orientation == Qt::Horizontal) {
        const int w = hr.width();
        minPos = gr.x() + w / 2;
        span = gr.width() - w;
    } else {
        const int h = hr.height();
        minPos = gr.y() + h / 2;
        span = gr.height() - h;
    }
    if (span <= 0)
        [[unlikely]]
        span = 1;

    return QStyle::sliderValueFromPosition(
        m_minimum, m_maximum, pos - minPos, span, opt.upsideDown);
}

// ---------------------------------------------------------------------------
// Painting — uses QStyle primitives exclusively
// ---------------------------------------------------------------------------

void QRangeSlider::paintEvent([[maybe_unused]] QPaintEvent* event)
{
    QStylePainter painter(this);

    // --- Unselected track ---
    // Styles fill the groove from the track start up to sliderPosition (or
    // from sliderPosition to the track end when upsideDown is set). Pinning
    // the position at the minimum would still leave an accent stub up to the
    // minimum handle center, so instead pick, per outer segment, a
    // sliderPosition/upsideDown combination whose accent fill falls entirely
    // outside the clipped region — leaving a pure unselected track.
    QStyleOptionSlider opt;
    initStyleOption(&opt, LowerHandle);
    opt.subControls = QStyle::SC_SliderGroove;

    const QRect groove = grooveRect();
    const QRect lowerRect = handleRect(LowerHandle);
    const QRect upperRect = handleRect(UpperHandle);

    // --- Selected-range region: the span between the two handle centers ---
    QRect selected;
    if (m_orientation == Qt::Horizontal) [[likely]] {
        selected = QRect(QPoint(lowerRect.center().x(), groove.top()),
            QPoint(upperRect.center().x(), groove.bottom()));
    } else {
        // Vertical: lower value sits at the bottom (upsideDown == true).
        selected = QRect(QPoint(groove.left(), upperRect.center().y()),
            QPoint(groove.right(), lowerRect.center().y()));
    }
    selected = selected.intersected(groove);

    // Track segment before the selected range (left for horizontal, bottom
    // for vertical) and the one after it (right / top).
    const bool horizontal = (m_orientation == Qt::Horizontal);
    const QRect beforeSelected = horizontal
        ? QRect(QPoint(groove.left(), groove.top()),
              QPoint(selected.left(), groove.bottom()))
        : QRect(QPoint(groove.left(), selected.bottom()),
              QPoint(groove.right(), groove.bottom()));
    const QRect afterSelected = horizontal
        ? QRect(QPoint(selected.right(), groove.top()),
              QPoint(groove.right(), groove.bottom()))
        : QRect(QPoint(groove.left(), groove.top()),
              QPoint(groove.right(), selected.top()));

    const auto paintTrack = [&](const QRect& region, bool flippedUpsideDown) {
        if (!region.isValid() || region.isEmpty())
            return;
        QStyleOptionSlider trackOpt = opt;
        trackOpt.sliderPosition = m_lowerValue;
        trackOpt.sliderValue = m_lowerValue;
        trackOpt.upsideDown = flippedUpsideDown ? !opt.upsideDown : opt.upsideDown;
        painter.save();
        painter.setClipRect(region);
        painter.drawComplexControl(QStyle::CC_Slider, trackOpt);
        painter.restore();
    };

    // With upsideDown flipped, the accent fill spans [lower handle, track
    // end] — entirely outside the leading segment.
    paintTrack(beforeSelected, /*flippedUpsideDown=*/true);
    // With the normal orientation, the accent fill spans [track start, lower
    // handle] — entirely outside the trailing segment.
    paintTrack(afterSelected, /*flippedUpsideDown=*/false);

    // --- Selected-range fill ---
    // Re-draw the groove with sliderPosition = upper value, clipped to the
    // span between the two handles: the style's fill-to-position then covers
    // exactly [lower handle, upper handle] with the normal selected look.
    if (selected.isValid() && !selected.isEmpty()) [[likely]] {
        QStyleOptionSlider fillOpt = opt;
        fillOpt.sliderPosition = m_upperValue;
        fillOpt.sliderValue = m_upperValue;
        painter.save();
        painter.setClipRect(selected);
        painter.drawComplexControl(QStyle::CC_Slider, fillOpt);
        painter.restore();
    }

    // --- Handles ---
    opt.subControls = QStyle::SC_SliderHandle;

    const auto paintHandle = [&](Handle which) {
        opt.sliderPosition = (which == UpperHandle) ? m_upperValue : m_lowerValue;
        opt.state &= ~(QStyle::State_Sunken | QStyle::State_MouseOver);
        opt.activeSubControls = QStyle::SC_None;

        if (m_pressedHandle == which) {
            opt.state |= QStyle::State_Sunken;
            opt.activeSubControls = QStyle::SC_SliderHandle;
        } else if (m_hoverHandle == which) {
            opt.state |= QStyle::State_MouseOver;
        } else if (hasFocus() && m_focusHandle == which) {
            opt.state |= QStyle::State_HasFocus;
            opt.activeSubControls = QStyle::SC_SliderHandle;
        }

        painter.drawComplexControl(QStyle::CC_Slider, opt);
    };

    paintHandle(LowerHandle);
    paintHandle(UpperHandle);
}

// ---------------------------------------------------------------------------
// Mouse interaction
// ---------------------------------------------------------------------------

void QRangeSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) [[unlikely]] {
        QWidget::mousePressEvent(event);
        return;
    }
    event->accept();

    const QPoint pos = event->position().toPoint();
    Handle h = handleAt(pos);

    if (h == NoHandle) {
        // Click on the groove — move the nearest handle to the click.
        const QRect gr = grooveRect();
        if (gr.contains(pos)) {
            const int pixelPos = (m_orientation == Qt::Horizontal)
                ? pos.x()
                : pos.y();
            const int val = pixelPosToRangeValue(pixelPos);
            const int lowerDist = qAbs(val - m_lowerValue);
            const int upperDist = qAbs(val - m_upperValue);
            h = (lowerDist <= upperDist) ? LowerHandle : UpperHandle;
            if (h == LowerHandle)
                setLowerValue(val);
            else
                setUpperValue(val);
        }
    }

    if (h != NoHandle) {
        m_pressedHandle = h;
        m_focusHandle = h;
        update();
        Q_EMIT sliderPressed(h);
    }
}

void QRangeSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressedHandle == NoHandle) {
        // Hover tracking.
        const Handle h = handleAt(event->position().toPoint());
        if (h != m_hoverHandle) {
            m_hoverHandle = h;
            update();
        }
        return;
    }

    event->accept();
    const int pixelPos = (m_orientation == Qt::Horizontal)
        ? qRound(event->position().x())
        : qRound(event->position().y());
    int val = pixelPosToRangeValue(pixelPos);
    val = qBound(m_minimum, val, m_maximum);

    if (m_pressedHandle == LowerHandle) {
        val = qMin(val, m_upperValue);
        if (val != m_lowerValue) {
            m_lowerValue = val;
            update();
            Q_EMIT lowerValueChanged(val);
            Q_EMIT sliderMoved(LowerHandle, val);
        }
    } else {
        val = qMax(val, m_lowerValue);
        if (val != m_upperValue) {
            m_upperValue = val;
            update();
            Q_EMIT upperValueChanged(val);
            Q_EMIT sliderMoved(UpperHandle, val);
        }
    }
}

void QRangeSlider::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_pressedHandle == NoHandle) [[unlikely]] {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    event->accept();
    const Handle h = m_pressedHandle;
    m_pressedHandle = NoHandle;
    update();
    Q_EMIT sliderReleased(h);
}

// ---------------------------------------------------------------------------
// Keyboard interaction
// ---------------------------------------------------------------------------

void QRangeSlider::keyPressEvent(QKeyEvent* event)
{
    const Handle handle = m_focusHandle;
    const int step = (event->modifiers() & Qt::ShiftModifier)
        ? m_singleStep * 10
        : m_singleStep;

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
        if (handle == LowerHandle)
            setLowerValue(m_lowerValue - step);
        else
            setUpperValue(m_upperValue - step);
        event->accept();
        break;
    case Qt::Key_Right:
    case Qt::Key_Up:
        if (handle == LowerHandle)
            setLowerValue(m_lowerValue + step);
        else
            setUpperValue(m_upperValue + step);
        event->accept();
        break;
    case Qt::Key_PageDown:
        if (handle == LowerHandle)
            setLowerValue(m_lowerValue - step * 10);
        else
            setUpperValue(m_upperValue - step * 10);
        event->accept();
        break;
    case Qt::Key_PageUp:
        if (handle == LowerHandle)
            setLowerValue(m_lowerValue + step * 10);
        else
            setUpperValue(m_upperValue + step * 10);
        event->accept();
        break;
    case Qt::Key_Home:
        if (handle == LowerHandle)
            setLowerValue(m_minimum);
        else
            setUpperValue(m_lowerValue);
        event->accept();
        break;
    case Qt::Key_End:
        if (handle == LowerHandle)
            setLowerValue(m_upperValue);
        else
            setUpperValue(m_maximum);
        event->accept();
        break;
    default:
        [[unlikely]] QWidget::keyPressEvent(event);
    }
}

void QRangeSlider::wheelEvent(QWheelEvent* event)
{
    event->accept();
    const Handle handle = (m_pressedHandle != NoHandle)
        ? m_pressedHandle
        : m_focusHandle;
    const int delta = (event->angleDelta().y() > 0) ? m_singleStep : -m_singleStep;

    if (handle == LowerHandle)
        setLowerValue(m_lowerValue + delta);
    else
        setUpperValue(m_upperValue + delta);
}

// ---------------------------------------------------------------------------
// Focus / hover / style-change housekeeping
// ---------------------------------------------------------------------------

void QRangeSlider::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    update();
}

void QRangeSlider::leaveEvent(QEvent* event)
{
    if (m_hoverHandle != NoHandle) {
        m_hoverHandle = NoHandle;
        update();
    }
    QWidget::leaveEvent(event);
}

void QRangeSlider::changeEvent(QEvent* event)
{
    switch (event->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::FontChange:
        update();
        updateGeometry();
        break;
    default:
        break;
    }
    QWidget::changeEvent(event);
}

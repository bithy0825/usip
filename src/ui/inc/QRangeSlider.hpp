#pragma once

#include <QPair>
#include <QWidget>

class QStyleOptionSlider;

// =============================================================================
// QRangeSlider — a dual-handle range slider widget.
//
// Renders entirely through QStyle complex-control primitives (CC_Slider),
// so it follows the active style / stylesheet exactly like QSlider.
//
// API mirrors QAbstractSlider/QSlider conventions:
//   slider.setMinimum(0);
//   slider.setMaximum(255);
//   slider.setValues(30, 200);
//   connect(&slider, &QRangeSlider::lowerValueChanged, ...);
//   connect(&slider, &QRangeSlider::upperValueChanged, ...);
// =============================================================================
class QRangeSlider : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int minimum READ minimum WRITE setMinimum NOTIFY rangeChanged)
    Q_PROPERTY(int maximum READ maximum WRITE setMaximum NOTIFY rangeChanged)
    Q_PROPERTY(int lowerValue READ lowerValue WRITE setLowerValue NOTIFY lowerValueChanged)
    Q_PROPERTY(int upperValue READ upperValue WRITE setUpperValue NOTIFY upperValueChanged)
    Q_PROPERTY(Qt::Orientation orientation READ orientation WRITE setOrientation NOTIFY orientationChanged)
    Q_PROPERTY(int singleStep READ singleStep WRITE setSingleStep)

public:
    enum Handle { NoHandle,
        LowerHandle,
        UpperHandle };
    Q_ENUM(Handle)

    explicit QRangeSlider(QWidget* parent = nullptr);
    explicit QRangeSlider(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~QRangeSlider() override;

    QRangeSlider(const QRangeSlider&) = delete;
    QRangeSlider& operator=(const QRangeSlider&) = delete;

    [[nodiscard]] int minimum() const;
    [[nodiscard]] int maximum() const;
    [[nodiscard]] int lowerValue() const;
    [[nodiscard]] int upperValue() const;
    [[nodiscard]] QPair<int, int> values() const;
    [[nodiscard]] Qt::Orientation orientation() const;
    [[nodiscard]] int singleStep() const;

    void setRange(int min, int max);
    void setValues(int lower, int upper);
    void setMinimum(int min);
    void setMaximum(int max);
    void setLowerValue(int value);
    void setUpperValue(int value);
    void setOrientation(Qt::Orientation orientation);
    void setSingleStep(int step);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void rangeChanged(int min, int max);
    void lowerValueChanged(int value);
    void upperValueChanged(int value);
    void orientationChanged(Qt::Orientation orientation);
    void sliderPressed(QRangeSlider::Handle handle);
    void sliderMoved(QRangeSlider::Handle handle, int value);
    void sliderReleased(QRangeSlider::Handle handle);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void initStyleOption(QStyleOptionSlider* option, Handle handle) const;
    QRect grooveRect() const;
    QRect handleRect(Handle handle) const;
    Handle handleAt(const QPoint& pos) const;
    int pixelPosToRangeValue(int pos) const;

    Qt::Orientation m_orientation = Qt::Horizontal;
    int m_minimum = 0;
    int m_maximum = 99;
    int m_lowerValue = 0;
    int m_upperValue = 99;
    int m_singleStep = 1;

    Handle m_pressedHandle = NoHandle;
    Handle m_hoverHandle = NoHandle;
    Handle m_focusHandle = LowerHandle;
};

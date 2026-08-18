#pragma once

#include <QColor>
#include <QToolBar>

#include "ui_protocol.hpp"

class QAction;
class QActionGroup;
class QSpinBox;
class QStackedWidget;
class QRangeSlider;
class QSlider;
class QToolButton;
class QWidget;

namespace usip::ui {

class menu_bar;
class draw_options;
class mask_options;
class measure_options;

class options_tool_bar : public ui_protocol<options_tool_bar, QToolBar> {
    Q_OBJECT
    friend class ui_protocol<options_tool_bar, QToolBar>;

public:
    explicit options_tool_bar(menu_bar& menu, cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~options_tool_bar() override;

    options_tool_bar(const options_tool_bar&) = delete;
    options_tool_bar& operator=(const options_tool_bar&) = delete;
    options_tool_bar(options_tool_bar&&) = delete;
    options_tool_bar& operator=(options_tool_bar&&) = delete;

    // ── Action 导出(选项页共享,禁止重复创建) ──────────────────────
    [[nodiscard]] QAction* apply_action() const noexcept { return apply_; }
    [[nodiscard]] QAction* cancel_action() const noexcept { return cancel_; }

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    void on_rectangle_draw_requested();
    void on_ellipse_draw_requested();
    void on_polygon_draw_requested();
    void on_threshold_segment_requested();
    void on_measure_requested();

private:
    menu_bar& menu_bar_;

    QActionGroup* view_group_ { nullptr };
    QAction* view_none_ { nullptr };
    QAction* view_split_ { nullptr };
    QAction* view_slider_ { nullptr };
    QAction* view_highlight_ { nullptr };
    QAction* view_difference_ { nullptr };

    QAction* apply_ { nullptr };
    QAction* cancel_ { nullptr };

    QSpinBox* page_control_ { nullptr };

    QStackedWidget* options_stack_ { nullptr };

    QWidget* empty_ { nullptr };

    draw_options* draw_options_ { nullptr };
    mask_options* mask_options_ { nullptr };
    measure_options* measure_options_ { nullptr };
};

class draw_options : public ui_protocol<draw_options, QToolBar> {
    Q_OBJECT
    friend class ui_protocol<draw_options, QToolBar>;

public:
    explicit draw_options(options_tool_bar& opt_tool_bar, cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~draw_options() override;

    draw_options(const draw_options&) = delete;
    draw_options& operator=(const draw_options&) = delete;
    draw_options(draw_options&&) = delete;
    draw_options& operator=(draw_options&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    options_tool_bar& opt_tool_bar_;
};

class mask_options : public ui_protocol<mask_options, QToolBar> {
    Q_OBJECT
    friend class ui_protocol<mask_options, QToolBar>;

public:
    explicit mask_options(options_tool_bar& opt_tool_bar, cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~mask_options() override;

    mask_options(const mask_options&) = delete;
    mask_options& operator=(const mask_options&) = delete;
    mask_options(mask_options&&) = delete;
    mask_options& operator=(mask_options&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    options_tool_bar& opt_tool_bar_;

    QSpinBox* floor_ { nullptr };
    QSpinBox* ceil_ { nullptr };
    QRangeSlider* threshold_ { nullptr };
    QToolButton* color_ { nullptr };
    QSlider* opacity_ { nullptr };
};

class measure_options : public ui_protocol<measure_options, QToolBar> {
    Q_OBJECT
    friend class ui_protocol<measure_options, QToolBar>;

public:
    explicit measure_options(options_tool_bar& opt_tool_bar, cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~measure_options() override;

    measure_options(const measure_options&) = delete;
    measure_options& operator=(const measure_options&) = delete;
    measure_options(measure_options&&) = delete;
    measure_options& operator=(measure_options&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    options_tool_bar& opt_tool_bar_;

    QSpinBox* line_width_ { nullptr };
    QToolButton* color_ { nullptr };
    QSlider* opacity_ { nullptr };
};

} // namespace usip::ui

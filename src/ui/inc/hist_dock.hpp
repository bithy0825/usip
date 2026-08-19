#pragma once

#include <QDockWidget>

#include <array>
#include <cstdint>

#include "event.hpp"
#include "ui_protocol.hpp"

class QBarSet;
class QChartView;
class QComboBox;
class QToolButton;
class QValueAxis;
class QWidget;

namespace usip::ui {

class hist_dock : public ui_protocol<hist_dock, QDockWidget> {
    Q_OBJECT
    friend class ui_protocol<hist_dock, QDockWidget>;

public:
    explicit hist_dock(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~hist_dock() override;

    hist_dock(const hist_dock&) = delete;
    hist_dock& operator=(const hist_dock&) = delete;
    hist_dock(hist_dock&&) = delete;
    hist_dock& operator=(hist_dock&&) = delete;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    // 总线回调:就绪/切换同一处理,重喂激活页直方图
    void on_document_changed(const cbuspp::value<std::shared_ptr<core::document>>& value);

    // bins_ → bars_(Percent 模式换算);top_raw_(计数域纵轴顶值)→ y 轴
    void refill_bars();
    void apply_y_axis();
    void reset_view(); // 排除 0 像素,最高条 ≈ 90% 绘图区高

private:
    QToolButton* add_btn_ { nullptr };
    QToolButton* sub_btn_ { nullptr };
    QToolButton* reset_btn_ { nullptr };
    QComboBox* mode_combo_ { nullptr };
    QChartView* hist_view_ { nullptr };
    QBarSet* bars_ { nullptr };
    QValueAxis* y_axis_ { nullptr };

    std::array<std::uint64_t, common::histogram::bin_count> bins_ { };
    std::uint64_t total_ { 0 }; // Σ bins(Percent 分母)
    double top_raw_ { 1.0 }; // 纵轴顶值(计数域;zoom/reset 仅改它)
};

}

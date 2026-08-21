#pragma once

#include <QDockWidget>
#include <QPointF>

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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

    void on_document_changed(const cbuspp::value<std::shared_ptr<core::document>>& value);
    // 文档关闭:就地清空(有剩余文档则随随后的 document_switch 重喂)
    void on_document_closed(const cbuspp::value<cuuidpp::uuid>& value);

    // 纵轴唯一事实源是 y_axis_(鼠标交互也直接改轴),按钮/复位/模式在其上操作
    [[nodiscard]] bool percent_mode() const; // 当前 Count/Percent
    [[nodiscard]] auto display(double count) const -> double; // 计数域→显示域
    void refill_bars(); // bins_ → bars_(按模式换算)
    void zoom_view(double factor); // factor < 1 放大(条形更高)
    void reset_view(); // 还原两轴;排除 0 像素,最高条 ≈ 90% 高

private:
    QToolButton* add_btn_ { nullptr };
    QToolButton* sub_btn_ { nullptr };
    QToolButton* reset_btn_ { nullptr };
    QComboBox* mode_combo_ { nullptr };
    QChartView* hist_view_ { nullptr };
    QBarSet* bars_ { nullptr };
    QValueAxis* x_axis_ { nullptr };
    QValueAxis* y_axis_ { nullptr };

    std::array<std::uint64_t, common::histogram::bin_count> bins_ { };
    std::uint64_t total_ { 0 }; // Σ bins(Percent 分母)
    bool percent_ { false };

    bool panning_ { false }; // 左键拖动平移
    QPointF pan_last_ { };
};

}

#pragma once

#include <QMenuBar>
#include <filesystem>

#include "ui_protocol.hpp"

class QMenu;
class QAction;

namespace usip::ui {

class menu_bar : public ui_protocol<menu_bar, QMenuBar> {
    Q_OBJECT
    friend class ui_protocol<menu_bar, QMenuBar>;

public:
    explicit menu_bar(cbuspp::bus<common::executor>& bus, QWidget* parent = nullptr);
    ~menu_bar() override;

    menu_bar(const menu_bar&) = delete;
    menu_bar& operator=(const menu_bar&) = delete;
    menu_bar(menu_bar&&) = delete;
    menu_bar& operator=(menu_bar&&) = delete;

    // ── Action 导出(工具栏共享,禁止重复创建) ──────────────────────────
    [[nodiscard]] QAction* open_action() const noexcept { return open_; }
    [[nodiscard]] QAction* save_action() const noexcept { return save_; }
    [[nodiscard]] QAction* pseudocolor_action() const noexcept { return pseudocolor_; }
    [[nodiscard]] QAction* mask_action() const noexcept { return mask_; }
    [[nodiscard]] QAction* zero_is_black_action() const noexcept { return zero_is_black_; }
    [[nodiscard]] QAction* exit_action() const noexcept { return exit_; }
    [[nodiscard]] QAction* export_action() const noexcept { return export_; }
    [[nodiscard]] QAction* save_as_action() const noexcept { return save_as_; }
    [[nodiscard]] QAction* close_action() const noexcept { return close_; }
    [[nodiscard]] QAction* about_action() const noexcept { return about_; }
    [[nodiscard]] QAction* clear_constituency_action() const noexcept { return clear_constituency_; }
    [[nodiscard]] QAction* clear_measurements_action() const noexcept { return clear_measurements_; }

private:
    void setup_ui();
    void setup_subscriptions();
    void setup_connections();

private:
    void on_file_selected(const cbuspp::value<std::filesystem::path>& path);

    void add_recent_file(const std::filesystem::path& path);
    void open_recent_file(const std::filesystem::path& path);
    void update_recent_menu();

private:
    QAction* open_ { nullptr };
    QMenu* recent_ { nullptr };
    QAction* save_ { nullptr };
    QAction* save_as_ { nullptr };
    QAction* export_ { nullptr };
    QAction* close_ { nullptr };
    QAction* exit_ { nullptr };

    QAction* pseudocolor_ { nullptr };
    QAction* mask_ { nullptr };
    QAction* zero_is_black_ { nullptr };
    QAction* clear_constituency_ { nullptr };
    QAction* clear_measurements_ { nullptr };

    QAction* about_ { nullptr };
};

}

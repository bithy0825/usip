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
    QAction* clear_constituency_ { nullptr };
    QAction* clear_measurements_ { nullptr };

    QAction* about_ { nullptr };
};

}

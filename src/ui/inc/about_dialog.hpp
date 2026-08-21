#pragma once

#include <QDialog>

namespace usip::ui {

// ─── About 对话框:应用身份 / 版本 / 运行环境 / 版权(只读展示,模态)──────────
class about_dialog : public QDialog {
    Q_OBJECT

public:
    explicit about_dialog(QWidget* parent = nullptr);
    ~about_dialog() override;

    about_dialog(const about_dialog&) = delete;
    about_dialog& operator=(const about_dialog&) = delete;
    about_dialog(about_dialog&&) = delete;
    about_dialog& operator=(about_dialog&&) = delete;

private:
    void setup_ui();
};

}

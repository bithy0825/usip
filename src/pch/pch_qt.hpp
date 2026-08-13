// =============================================================================
// pch_qt.hpp — PCH 并行片段:Qt 常用头
//
// 与其他片段完全解耦;在 CMake 层按需组合:
//   usip_apply_pch(<target>)
// =============================================================================
#pragma once

// ---- QtCore ------------------------------------------------------------------
#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

// ---- QtWidgets ---------------------------------------------------------------
#include <QApplication>
#include <QDialog>
#include <QMainWindow>
#include <QWidget>

#include <QIcon>
#include <QPixmap>

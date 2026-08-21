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
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSettings>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

// ---- QtGui -------------------------------------------------------------------
#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QCursor>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QResizeEvent>
#include <QTransform>
#include <QWheelEvent>

// ---- QtWidgets ---------------------------------------------------------------
#include <QAbstractSpinBox>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOption>
#include <QStylePainter>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QWidget>

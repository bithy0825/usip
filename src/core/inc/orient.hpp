#pragma once

// ==============================================================================
// orient.hpp — TIFF orientation 显示变换(渲染/统计/取样共用;纯函数,头文件内联)
//
// 显示域 = 存储域经 orient_transform 变换(L1/L3 渲染、选区统计栅格化均以此
// 对齐);display_to_storage 为其逆(含 QImage::transformed 的包围盒归一平移),
// 供状态栏取样等"显示坐标 → 存储像素"场景使用。
// ==============================================================================

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QTransform>

#include <utility>

#include "tiff.hpp"

namespace usip::core {

// TIFF orientation → 显示变换(基线 top_left 恒等)
[[nodiscard]] inline auto orient_transform(common::orientation orient) -> QTransform
{
    using enum common::orientation;
    switch (orient) {
    [[likely]] case top_left:
        return { };
    case top_right:
        return QTransform::fromScale(-1.0, 1.0); // 水平镜像
    case bottom_right:
        return QTransform::fromScale(-1.0, -1.0); // 旋转 180°
    case bottom_left:
        return QTransform::fromScale(1.0, -1.0); // 垂直镜像
    case left_top:
        return { 0.0, 1.0, 1.0, 0.0, 0.0, 0.0 }; // 转置
    case right_top: {
        QTransform t;
        t.rotate(90.0);
        return t;
    }
    case right_bottom:
        return { 0.0, -1.0, -1.0, 0.0, 0.0, 0.0 }; // 反转置
    case left_bottom: {
        QTransform t;
        t.rotate(270.0);
        return t;
    }
    }
    std::unreachable();
}

// 显示域连续坐标 → 存储域像素索引:orient 矩阵 + QImage::transformed 同款包围盒
// 归一平移的合成逆变换(镜像/90° 旋转型恒可逆且整数精确);越界由调用方按存储
// 尺寸判定
[[nodiscard]] inline auto display_to_storage(QPointF disp, common::orientation orient,
    const QSize& storage) -> QPoint
{
    const QTransform m = orient_transform(orient);
    const QRectF box = m.mapRect(QRectF { 0.0, 0.0, static_cast<double>(storage.width()),
        static_cast<double>(storage.height()) });
    // 行向量约定:A*B = 先 A 后 B(同 canvas 视图变换)
    const QTransform full = m * QTransform::fromTranslate(-box.x(), -box.y());
    bool invertible = false;
    const QTransform inv = full.inverted(&invertible);
    if (!invertible) [[unlikely]]
        return { -1, -1 };
    const QPointF raw = inv.map(disp);
    return { static_cast<int>(raw.x()), static_cast<int>(raw.y()) };
}

}

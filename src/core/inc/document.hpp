#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QImage>
#include <QPointF>

#include <clipper2/clipper.h>
#include <cuuidpp/cuuidpp.hpp>

#include "tiff.hpp"

namespace usip::core {

// ─── ROI:矢量选区(图像像素坐标);渲染时动态绘制边框/填充,不存光栅 ──────────
struct roi {
    Clipper2Lib::PathsD path;
};

// ─── 标注:两点 + 标签(如 "35.96mm";由测量方按 dpi 换算后写入)──────────────
struct annotation {
    std::pair<QPointF, QPointF> line { }; // 两点
    std::string label { };
};

// ─── mask:阈值分割蒙版 ──────────────────────────────────────────────────
struct mask {
    QImage image { }; // 8-bit 灰度,0=黑,255=白
    std::pair<double, double> range { 0, 255 }; // 阈值分割上下限
};

// ─── page:显示基本单元 ─────────────────────────────────────────────────────
struct page {
    // 身份与来源
    common::page_info info; // 文件元数据(io 产出;info.id 即页 uuid)
    cuuidpp::uuid doc_id { }; // 所属文档 uuid(= tiff_info.id)
    std::uint32_t index { 0 }; // 页序

    // 原始数据(设备规则已应用;zero_is_white/orient 不碰像素,归显示管线)
    QImage image { };

    // 阈值分割蒙版:惰性初始化(首页创建时给全有效;其余页切换到时再补),
    // 未初始化 = 无 mask
    std::optional<mask> mask { };

    std::vector<roi> rois { };
    std::vector<annotation> annotations { };

    std::optional<cuuidpp::uuid> compare_to { }; // 对比页 uuid(跨文档亦可)
};

// ─── document:轻宿主;像素在各 page 的 QImage 里,这里只有页集合 ──────────────
struct document {
    common::tiff_info info { };
    std::unordered_map<cuuidpp::uuid, std::shared_ptr<page>> pages { }; // key=page_info.id
    std::pair<double, double> step { 1.0, 1.0 }; // 采集步长
    cuuidpp::uuid active_page { }; // 当前页 uuid(= page_info.id)
};

}

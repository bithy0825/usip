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
    Clipper2Lib::PathD path;
    bool visible { true };
};

// ─── 标注:两点 + 标签(如 "35.96mm";由测量方按 dpi 换算后写入)──────────────
struct annotation {
    QPointF a { };
    QPointF b { };
    std::string label { };
};

// ─── mask:阈值分割蒙版 ──────────────────────────────────────────────────
struct mask {
    QImage image { }; // 8-bit 灰度,0=黑,255=白
    std::pair<int, int> range { 0, 255 }; // 阈值分割上下限
    bool visible { true }; // 显示开关
};

// ─── page:显示基本单元 ─────────────────────────────────────────────────────
struct page {
    // 身份与来源
    common::page_info info; // 文件元数据(io 产出;info.id 即页 uuid)
    cuuidpp::uuid doc_id { }; // 所属文档 uuid(= tiff_info.id)
    std::uint32_t index { 0 }; // 页序:运行期视图状态,故不放 page_info

    // 原始数据(设备规则已应用;zero_is_white/orient 不碰像素,归显示管线)
    QImage image { };

    mask mask { }; // 阈值分割蒙版(8-bit 灰度,0=黑,255=白)

    std::vector<roi> rois { };
    std::vector<annotation> annotations { };

    std::optional<cuuidpp::uuid> compare_to { }; // 对比页 uuid(跨文档亦可)
};

// ─── document:轻宿主;像素在各 page 的 QImage 里,这里只有页集合 ──────────────
struct document {
    common::tiff_info info { };
    std::unordered_map<cuuidpp::uuid, std::shared_ptr<page>> pages { }; // key=page_info.id
    cuuidpp::uuid active_page { }; // 当前页 uuid(= page_info.id)
};

}

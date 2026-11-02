#pragma once

#include "NSlib/App/Application.h"

#include <string_view>

//! 窓を見せない試し用の Application の設定を作る
inline NS::ApplicationDesc HiddenAppDesc(std::string_view title, const NS::Size2D& size)
{
    NS::ApplicationDesc desc;
    desc.window.title = title;
    desc.window.size = size;
    desc.window.visible = false;
    return desc;
}

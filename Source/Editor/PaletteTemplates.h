#pragma once

#include "NSlib/CoreObject/ActorJson.h"

#include <array>

namespace NS
{
    namespace Editor
    {
        /// 配置用オブジェクトのテンプレート情報
        struct PaletteTemplate
        {
            const char* name = nullptr;

            bool rotatable = false;

            /// 配置時に複製して使うひな形の JSON
            nlohmann::json prototype;
        };

        inline constexpr std::size_t k_PaletteSlotCount = 3;

        /// 初めの 1 回だけ置く物の一覧から組む。組む時に .actor を読むので投げうる
        [[nodiscard]] const std::array<PaletteTemplate, k_PaletteSlotCount>& PaletteTemplateSlots();
    } // namespace Editor
} // namespace NS

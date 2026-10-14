#pragma once

#include "NSlib/Object/ObjectJson.h"

#include <array>

namespace NS::Editor
{
    //! @brief 配置用オブジェクトのテンプレート情報
    struct PaletteTemplate
    {
        const char* name = nullptr;

        bool rotatable = false;

        //! 配置時に複製して使うひな形の JSON
        nlohmann::json prototype;
    };

    inline constexpr std::size_t k_PaletteSlotCount = 3;

    [[nodiscard]] const std::array<PaletteTemplate, k_PaletteSlotCount>& PaletteTemplateSlots() noexcept;

} // namespace NS::Editor

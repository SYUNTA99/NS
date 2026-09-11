#pragma once

#include "Runtime/Object/Scene/SceneData.h"

#include <array>

namespace NS::Editor
{
    //! @brief 配置用オブジェクトのテンプレート情報
    struct PaletteTemplate
    {
        //! ツールバーなどのUIに表示する名称
        const char* name = nullptr;

        //! 配置時に回転操作を許可するかどうか
        bool rotatable = false;

        //! 配置時に複製して使用するオブジェクトデータ
        NS::Object::ObjectData prototype;
    };

    //! パレットのブラシの総数
    inline constexpr std::size_t k_PaletteSlotCount = 3;

    //! @brief 利用可能な配置テンプレートの一覧を取得する
    [[nodiscard]] const std::array<PaletteTemplate, k_PaletteSlotCount>& PaletteTemplateSlots() noexcept;

} // namespace NS::Editor
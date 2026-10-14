#pragma once

#include "NSlib/Core/Math.h"

#include "NSlib/Core/Types.h"
#include <array>
#include <optional>

namespace NS::Gfx
{
    //! @brief EffectScene::Play が返す、再生したエフェクトのハンドル
    struct EffectHandle
    {
        NS::int32 value = -1; //!< Effekseer の Manager が返したハンドル値。既定の -1 は無効

        //! value が 0 以上の場合 true、それ以外の場合は false
        //! @details エフェクトが今も残っているかは EffectScene::Exists で見る
        [[nodiscard]] bool IsValid() const noexcept { return value >= 0; }
    };

    //! @brief EffectScene::Play に渡す、再生の姿勢と見た目
    //! @details 既定の値で渡すと、原点で絵の定義どおりに出す
    struct EffectPlayDesc
    {
        NS::Vector3 position{0.0f, 0.0f, 0.0f};             //!< 再生位置のワールド座標
        NS::Quaternion rotation = NS::Quaternion::Identity; //!< 絵の向き
        NS::Vector3 scale{1.0f, 1.0f, 1.0f};                //!< 絵の大きさの倍率。軸ごとに掛ける
        //! 絵の全体に掛ける色。各成分 0〜1 で、白は定義の色のまま
        NS::Color color{1.0f, 1.0f, 1.0f, 1.0f};
        //! 動的入力 0〜3 番の値。書いた番号だけ、絵の定義が持つ既定の値を上書きする
        std::array<std::optional<float>, 4> dynamicInputs{};
    };
} // namespace NS::Gfx

#pragma once

#include "NSlib/Core/Math.h"

#include "NSlib/Core/Types.h"
#include <array>
#include <optional>

namespace NS
{
    namespace Gfx
    {
        struct EffectHandle
        {
            int32 value = -1;

            //! 再生中かの生存確認は EffectScene::Exists を使う
            [[nodiscard]] bool IsValid() const noexcept { return value >= 0; }
        };

        struct EffectPlayDesc
        {
            Vector3 position{0.0f, 0.0f, 0.0f};
            Quaternion rotation = Quaternion::Identity;
            Vector3 scale{1.0f, 1.0f, 1.0f};
            Color color{1.0f, 1.0f, 1.0f, 1.0f}; // 全体に乗算する色 (各成分 0〜1)
            std::array<std::optional<float>, 4> dynamicInputs{}; // 動的入力 0〜3 の初期値 (指定した要素のみ既定値を上書き)
        };

        [[nodiscard]] inline EffectPlayDesc MakeEffectPlayDesc(const Vector3& position,
                                                               const Quaternion& rotation,
                                                               const Vector3& scale) noexcept
        {
            EffectPlayDesc desc;
            desc.position = position;
            desc.rotation = rotation;
            desc.scale = scale;
            return desc;
        }
    } // namespace Gfx
} // namespace NS

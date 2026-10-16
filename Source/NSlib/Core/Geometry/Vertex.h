#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"

#include <cstddef>
#include <type_traits>

namespace NS
{
    //! スキンメッシュで使えるボーンの最大数
    inline constexpr std::size_t k_MaxBones = 128;

    //! アニメーションを持たない静的モデル用の頂点データ構造
    struct StaticVertex
    {
        Vector3 position;
        Vector2 uv;
        Vector3 normal;
    };
    static_assert(sizeof(StaticVertex) == 32, "頂点レイアウトの制約上、StaticVertexは32バイトである必要があります。");
    static_assert(std::is_standard_layout_v<StaticVertex>,
                  "GPUへ正しく転送するため、StaticVertexは標準レイアウトである必要があります。");

    //! @brief スキンメッシュ用の頂点データ構造
    //! @details 頂点の基本情報に加え、最大4つのボーンからインデックスとウェイトを保持する
    struct SkinnedVertex
    {
        Vector3 position;
        Vector2 uv;
        Vector3 normal;
        uint32 joints[4];
        float weights[4];
    };
    static_assert(sizeof(SkinnedVertex) == 64, "頂点レイアウトの制約上、SkinnedVertexは64バイトである必要があります。");
    static_assert(std::is_standard_layout_v<SkinnedVertex>,
                  "GPUへ正しく転送するため、SkinnedVertexは標準レイアウトである必要があります。");
} // namespace NS

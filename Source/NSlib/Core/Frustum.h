#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"

#include <cmath>

namespace NS
{
    //! AABB カリング用の視錐台
    struct Frustum
    {
        Plane planes[6]{}; // 左・右・下・上・ニア・ファーの順（内側が正、非正規化）

        //! 行ベクトル規約 v*M のビュープロジェクション行列から 6 平面を抽出する（DX11 の NDC 奥行き [0, 1] 前提）
        [[nodiscard]] static Frustum FromViewProjection(const Matrix& m) noexcept
        {
            Frustum f{};
            f.planes[0] = Plane{m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41};
            f.planes[1] = Plane{m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41};
            f.planes[2] = Plane{m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42};
            f.planes[3] = Plane{m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42};
            f.planes[4] = Plane{m._13, m._23, m._33, m._43};
            f.planes[5] = Plane{m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43};
            return f;
        }

        //! 各平面に対する保守的な交差判定を行う（偽陰性なし）
        [[nodiscard]] bool Intersects(const AABB& box) const noexcept
        {
            for (const Plane& p : planes)
            {
                const float r =
                    box.Extents.x * std::fabs(p.x) + box.Extents.y * std::fabs(p.y) + box.Extents.z * std::fabs(p.z);
                const float s = p.x * box.Center.x + p.y * box.Center.y + p.z * box.Center.z + p.w;
                if (s + r < 0.0f)
                {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace NS

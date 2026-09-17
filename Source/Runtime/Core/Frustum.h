#pragma once

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"

#include <cmath>

namespace NS::Core
{
    //! @brief AABB カリング用の視錐台。ビュープロジェクション行列から抜いた 6 平面を、内側が正になる向きで持つ
    //! @details 平面は正規化しない。内外の符号だけ要る AABB 交差にしか使わないため、正規化は無駄になる
    struct Frustum
    {
        Plane planes[6]{}; // 左・右・下・上・ニア・ファーの順。各平面は内側が正

        //! @brief 行ベクトル規約 v*M のビュープロジェクション行列から 6 平面を抽出する。奥行きは DX11 の [0,1] 前提
        [[nodiscard]] static Frustum FromViewProjection(const Matrix& m) noexcept
        {
            Frustum f{};
            f.planes[0] = Plane{m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41};
            f.planes[1] = Plane{m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41};
            f.planes[2] = Plane{m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42};
            f.planes[3] = Plane{m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42};
            f.planes[4] = Plane{m._13, m._23, m._33, m._43}; // ニアは z>=0
            f.planes[5] = Plane{m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43};
            return f;
        }

        //! @brief AABB が視錐台と交差するか。完全に外なら false、少しでも重なれば true
        //! @details 中心・半径の保守判定で、外を内と誤ることはあっても内を外と切り落とすことはない
        [[nodiscard]] bool Intersects(const AABB& box) const noexcept
        {
            for (const Plane& p : planes)
            {
                const float r = box.Extents.x * std::fabs(p.x) + box.Extents.y * std::fabs(p.y) + box.Extents.z * std::fabs(p.z);
                const float s = p.x * box.Center.x + p.y * box.Center.y + p.z * box.Center.z + p.w;
                if (s + r < 0.0f)
                {
                    return false; // AABB 全体が内向き平面の外側なので視錐台の外
                }
            }
            return true;
        }
    };
} // namespace NS::Core

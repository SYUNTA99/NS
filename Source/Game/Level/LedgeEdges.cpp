#include "Game/Level/LedgeEdges.h"

#include "Runtime/Core/Math.h"

#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        using NS::Core::Dot;
        using NS::Core::Vector3;
        using NS::Core::OBB;

        // 内部ヘルパーの数学関数

        [[nodiscard]] bool ContainsPoint(const OBB& obb, const Vector3& p, float eps) noexcept
        {
            const Vector3 d{p.x - obb.center.x, p.y - obb.center.y, p.z - obb.center.z};
            return std::fabs(Dot(d, obb.axisX)) <= obb.halfExtentX + eps &&
                   std::fabs(Dot(d, obb.axisY)) <= obb.halfExtentY + eps &&
                   std::fabs(Dot(d, obb.axisZ)) <= obb.halfExtentZ + eps;
        }

        [[nodiscard]] Vector3 HorizontalUnit(const Vector3& v) noexcept
        {
            const float lenSq = v.x * v.x + v.z * v.z;
            if (lenSq < 1.0e-12f)
            {
                return Vector3{0.0f, 0.0f, 0.0f};
            }
            const float inv = 1.0f / std::sqrt(lenSq);
            return Vector3{v.x * inv, 0.0f, v.z * inv};
        }
    } // namespace

    std::vector<LedgeEdge> ComputeTopLedgeEdges(const std::vector<NS::Core::OBB>& solidBoxes)
    {
        constexpr float k_ContainEps = 1.0e-3f; // 面上も内側と判定する許容誤差
        constexpr float k_ProbeDist = 0.05f;    // 隣接判定で縁の外側を調べる距離。これ未満の隙間は地続き
        constexpr float k_CoverEps = 0.05f;     // 天面直上の被覆を調べる高さ

        const Vector3 worldUp{0.0f, 1.0f, 0.0f};
        std::vector<LedgeEdge> edges;

        for (std::size_t i = 0; i < solidBoxes.size(); ++i)
        {
            const OBB& box = solidBoxes[i];

            // 最も上を向いているローカル軸を天面の法線に選ぶ
            const Vector3 axes[3] = {box.axisX, box.axisY, box.axisZ};
            const float halfExtents[3] = {box.halfExtentX, box.halfExtentY, box.halfExtentZ};

            int topAxis = 1;
            float bestUp = std::fabs(Dot(box.axisY, worldUp));

            for (int k = 0; k < 3; ++k)
            {
                if (const float aligned = std::fabs(Dot(axes[k], worldUp)); aligned > bestUp)
                {
                    bestUp = aligned;
                    topAxis = k;
                }
            }

            // 選んだ軸の正負をワールド上方向に合わせる
            const float topSign = (Dot(axes[topAxis], worldUp) < 0.0f) ? -1.0f : 1.0f;
            const Vector3 up{axes[topAxis].x * topSign, axes[topAxis].y * topSign, axes[topAxis].z * topSign};
            const float upHalf = halfExtents[topAxis];

            const int inA = (topAxis + 1) % 3;
            const int inB = (topAxis + 2) % 3;

            const Vector3 faceCenter{
                box.center.x + up.x * upHalf, box.center.y + up.y * upHalf, box.center.z + up.z * upHalf};

            // 天面直上が別の固形で覆われていれば縁は無い
            const Vector3 aboveTop{faceCenter.x, faceCenter.y + k_CoverEps, faceCenter.z};
            bool isCovered = false;
            for (std::size_t j = 0; j < solidBoxes.size(); ++j)
            {
                if (i != j && ContainsPoint(solidBoxes[j], aboveTop, k_ContainEps))
                {
                    isCovered = true;
                    break;
                }
            }
            if (isCovered)
                continue;

            // 天面の 4 隅
            const Vector3& ax = axes[inA];
            const Vector3& az = axes[inB];
            const float hx = halfExtents[inA];
            const float hz = halfExtents[inB];

            const auto corner = [&](float sx, float sz) noexcept {
                return Vector3{faceCenter.x + ax.x * hx * sx + az.x * hz * sz,
                               faceCenter.y + ax.y * hx * sx + az.y * hz * sz,
                               faceCenter.z + ax.z * hx * sx + az.z * hz * sz};
            };

            const Vector3 cMinusMinus = corner(-1.0f, -1.0f);
            const Vector3 cPlusMinus = corner(+1.0f, -1.0f);
            const Vector3 cPlusPlus = corner(+1.0f, +1.0f);
            const Vector3 cMinusPlus = corner(-1.0f, +1.0f);

            // 外側に別の固形が接していない辺だけが踏み外せる縁になる
            const auto emitIfLedge = [&](const Vector3& a, const Vector3& b, const Vector3& axisOut) {
                const Vector3 outward = HorizontalUnit(axisOut);
                const Vector3 mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
                const Vector3 probe{
                    mid.x + outward.x * k_ProbeDist, mid.y + outward.y * k_ProbeDist, mid.z + outward.z * k_ProbeDist};

                for (std::size_t j = 0; j < solidBoxes.size(); ++j)
                {
                    if (i != j && ContainsPoint(solidBoxes[j], probe, k_ContainEps))
                    {
                        return; // 隣接ブロックがあるため踏み外せない
                    }
                }
                edges.push_back({a, b, outward});
            };

            emitIfLedge(cPlusMinus, cPlusPlus, ax);
            emitIfLedge(cMinusMinus, cMinusPlus, Vector3{-ax.x, -ax.y, -ax.z});
            emitIfLedge(cMinusPlus, cPlusPlus, az);
            emitIfLedge(cMinusMinus, cPlusMinus, Vector3{-az.x, -az.y, -az.z});
        }

        return edges;
    }
} // namespace NS::Game::Level
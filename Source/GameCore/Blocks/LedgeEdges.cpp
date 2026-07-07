#include "GameCore/Blocks/LedgeEdges.h"

#include <cmath>
#include <cstddef>

namespace NS::GameCore::Blocks
{
    namespace
    {
        using NS::Math::Vector3;
        using NS::Physics::OBB;

        [[nodiscard]] float Dot(const Vector3& a, const Vector3& b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        // p が obb の内側か。 各軸へ射影して半サイズ + eps に収まるかで判定する。 面上の点も内側に数える
        [[nodiscard]] bool ContainsPoint(const OBB& obb, const Vector3& p, float eps) noexcept
        {
            const Vector3 d{p.x - obb.center.x, p.y - obb.center.y, p.z - obb.center.z};
            return std::fabs(Dot(d, obb.axisX)) <= obb.halfExtents.x + eps &&
                   std::fabs(Dot(d, obb.axisY)) <= obb.halfExtents.y + eps &&
                   std::fabs(Dot(d, obb.axisZ)) <= obb.halfExtents.z + eps;
        }

        // v の水平成分を単位化する。 ほぼ鉛直なら zero を返す
        [[nodiscard]] Vector3 HorizontalUnit(const Vector3& v) noexcept
        {
            const float lenSq = v.x * v.x + v.z * v.z;
            if (lenSq < 1.0e-12f)
                return Vector3{0.0f, 0.0f, 0.0f};
            const float inv = 1.0f / std::sqrt(lenSq);
            return Vector3{v.x * inv, 0.0f, v.z * inv};
        }
    } // namespace

    std::vector<LedgeEdge> ComputeTopLedgeEdges(const std::vector<NS::Physics::OBB>& solidBoxes)
    {
        constexpr float kContainEps = 1.0e-3f; // 面上の点も内側と見なす許容
        constexpr float kProbeDist = 0.05f;    // 隣接判定で縁の外側へ差す距離。 これ未満の隙間は地続き扱い
        constexpr float kCoverEps = 0.05f;     // 天面直上の被覆を見る高さ

        const Vector3 worldUp{0.0f, 1.0f, 0.0f};

        std::vector<LedgeEdge> edges;
        for (std::size_t i = 0; i < solidBoxes.size(); ++i)
        {
            const OBB& box = solidBoxes[i];

            // 歩ける天面は 6 面のうち鉛直上と最もそろう面とみなす
            // axisY 固定では上下反転で底面を天面と誤認するため、 上を向く軸を選び直す
            const Vector3 axes[3] = {box.axisX, box.axisY, box.axisZ};
            const float halfByAxis[3] = {box.halfExtents.x, box.halfExtents.y, box.halfExtents.z};
            int topAxis = 1;
            float bestUp = std::fabs(Dot(box.axisY, worldUp));
            for (int k = 0; k < 3; ++k)
            {
                const float aligned = std::fabs(Dot(axes[k], worldUp));
                if (aligned > bestUp)
                {
                    bestUp = aligned;
                    topAxis = k;
                }
            }
            const float topSign = Dot(axes[topAxis], worldUp) >= 0.0f ? 1.0f : -1.0f;
            const Vector3 up{axes[topAxis].x * topSign, axes[topAxis].y * topSign, axes[topAxis].z * topSign};
            const float upHalf = halfByAxis[topAxis];
            const int inA = (topAxis + 1) % 3;
            const int inB = (topAxis + 2) % 3;

            const Vector3 faceCenter{
                box.center.x + up.x * upHalf, box.center.y + up.y * upHalf, box.center.z + up.z * upHalf};

            // 天面直上に別の固形があれば立てないので縁を出さない
            const Vector3 aboveTop{faceCenter.x, faceCenter.y + kCoverEps, faceCenter.z};
            bool covered = false;
            for (std::size_t j = 0; j < solidBoxes.size() && !covered; ++j)
                if (j != i && ContainsPoint(solidBoxes[j], aboveTop, kContainEps))
                    covered = true;
            if (covered)
                continue;

            const Vector3& ax = axes[inA];
            const Vector3& az = axes[inB];
            const float hx = halfByAxis[inA];
            const float hz = halfByAxis[inB];

            const auto corner = [&](float sx, float sz) noexcept {
                return Vector3{faceCenter.x + ax.x * hx * sx + az.x * hz * sz,
                               faceCenter.y + ax.y * hx * sx + az.y * hz * sz,
                               faceCenter.z + ax.z * hx * sx + az.z * hz * sz};
            };
            const Vector3 cMinusMinus = corner(-1.0f, -1.0f);
            const Vector3 cPlusMinus = corner(+1.0f, -1.0f);
            const Vector3 cPlusPlus = corner(+1.0f, +1.0f);
            const Vector3 cMinusPlus = corner(-1.0f, +1.0f);

            // 縁の外側へ差した点が別の固形に入っていなければ、 踏み外せる縁として足す
            const auto emitIfLedge = [&](const Vector3& a, const Vector3& b, const Vector3& axisOut) {
                const Vector3 outward = HorizontalUnit(axisOut);
                const Vector3 mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f};
                const Vector3 probe{
                    mid.x + outward.x * kProbeDist, mid.y + outward.y * kProbeDist, mid.z + outward.z * kProbeDist};
                for (std::size_t j = 0; j < solidBoxes.size(); ++j)
                    if (j != i && ContainsPoint(solidBoxes[j], probe, kContainEps))
                        return;
                edges.push_back(LedgeEdge{a, b, outward});
            };

            emitIfLedge(cPlusMinus, cPlusPlus, ax);
            emitIfLedge(cMinusMinus, cMinusPlus, Vector3{-ax.x, -ax.y, -ax.z});
            emitIfLedge(cMinusPlus, cPlusPlus, az);
            emitIfLedge(cMinusMinus, cPlusMinus, Vector3{-az.x, -az.y, -az.z});
        }
        return edges;
    }
} // namespace NS::GameCore::Blocks

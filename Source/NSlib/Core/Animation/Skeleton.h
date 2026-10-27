#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Geometry/Vertex.h"

#include <span>
#include <string>

namespace NS
{
    namespace Anim
    {
        struct BonePose
        {
            Vector3 translation{0.0f, 0.0f, 0.0f};
            Quaternion rotation{};
            Vector3 scale{1.0f, 1.0f, 1.0f};
        };

        struct Bone
        {
            int parentIndex = -1;     // 親ボーンの番号 (ルートは -1)
            Matrix inverseBind{};
            BonePose bindLocal{};
            std::string name;         // ボーン名 (アニメーションとの紐付けに使う)
        };

        //! ボーンの親子関係を持ち、ポーズからシェーダへ渡すボーンパレットを作る
        class Skeleton
        {
        public:
            Skeleton() = default;
            explicit Skeleton(std::vector<Bone> bones) noexcept;

            [[nodiscard]] std::size_t BoneCount() const noexcept;
            [[nodiscard]] const std::vector<Bone>& Bones() const noexcept;

            //! ポーズからシェーダ用のボーンパレットを計算する
            //! @param[in] pose 各ボーンのローカル姿勢。要素数は BoneCount() と一致すること
            void ComputePalette(std::span<const BonePose> pose, std::vector<Matrix>& out) const;

            void ComputeBindPalette(std::vector<Matrix>& out) const;

            //! 姿勢から各ボーンの、親の変換を全部掛けた行列を計算する
            //! 親は子より前に並ぶこと。親が後ろにあるボーンは根として扱う
            void ComputeGlobals(std::span<const BonePose> pose,
                                std::vector<Matrix>& out,
                                bool applyRootTransform = true) const;

            void SetRootTransform(const Matrix& transform) noexcept;

            [[nodiscard]] const Matrix& RootTransform() const noexcept;

            [[nodiscard]] static Vector3 SkinPositionReference(const SkinnedVertex& vertex,
                                                                   std::span<const Matrix> palette) noexcept;

        private:
            std::vector<Bone> m_bones;
            Matrix m_rootTransform{};
        };
    } // namespace Anim
} // namespace NS
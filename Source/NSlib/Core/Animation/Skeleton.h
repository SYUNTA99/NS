#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Geometry/Vertex.h"

#include <span>
#include <string>
#include <vector>

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
            int parentIndex = -1;     //!< 親ボーンの番号 (ルートは -1)
            Matrix inverseBind{};
            BonePose bindLocal{};
            std::string name;         //!< ボーン名 (アニメーションとの紐付けに使う)
        };

        //! @brief 2 つの姿勢を骨ごとに混ぜる。位置と拡大は線形、回転は球面で補間し、回転は短い方を回る
        //! @param[in] weight 0 で from、1 で to。0〜1 の外の値は 0〜1 に収める
        //! @param[out] out from と同じ数になる。from と to の骨の数が違う時は ERROR を出し、from をそのまま入れる
        void BlendPose(std::span<const BonePose> from,
                       std::span<const BonePose> to,
                       float weight,
                       std::vector<BonePose>& out);

        //! ボーンの親子関係を持ち、ポーズからシェーダへ渡すボーンパレットを作る
        class Skeleton
        {
        public:
            Skeleton() = default;
            explicit Skeleton(std::vector<Bone> bones) noexcept;

            [[nodiscard]] std::size_t BoneCount() const noexcept;
            [[nodiscard]] const std::vector<Bone>& Bones() const noexcept;

            //! @brief ポーズからシェーダ用のボーンパレットを計算する
            //! @param[in] pose 各ボーンのローカル姿勢。要素数は BoneCount() と一致すること
            void ComputePalette(std::span<const BonePose> pose, std::vector<Matrix>& out) const;

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
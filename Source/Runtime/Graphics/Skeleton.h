#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/SkeletalMesh.h"

#include <span>
#include <string>

namespace NS::Graphics
{
    //! ボーン 1 本の姿勢。位置・回転・スケールを持つ
    struct BonePose
    {
        NS::Core::Vector3 translation{0.0f, 0.0f, 0.0f};
        NS::Core::Quaternion rotation{};
        NS::Core::Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    //! スケルトンを構成する1つのボーン構造
    struct Bone
    {
        int parentIndex = -1;           //!< 親ボーンの番号 (ルートは -1)
        NS::Core::Matrix inverseBind{}; //!< バインドポーズの逆行列
        BonePose bindLocal{};           //!< バインドポーズのローカル変換
        std::string name;               //!< ボーン名 (アニメーションとの紐付けに使う)
    };

    //! @brief キャラクターの骨格
    //! @details ボーンの親子関係を持ち、ポーズからシェーダへ渡すボーンパレットを作る
    class Skeleton
    {
    public:
        Skeleton() = default;
        explicit Skeleton(std::vector<Bone> bones) noexcept;

        //! ボーンの総数を返す
        [[nodiscard]] std::size_t BoneCount() const noexcept;
        [[nodiscard]] const std::vector<Bone>& Bones() const noexcept;

        //! @brief ポーズからシェーダ用のボーンパレットを計算する
        //! @param[in] pose 各ボーンのローカル姿勢。要素数は BoneCount() と一致すること
        //! @param[out] out 計算結果の出力先
        void ComputePalette(std::span<const BonePose> pose, std::vector<NS::Core::Matrix>& out) const;

        //! バインドポーズのボーンパレットを計算する
        void ComputeBindPalette(std::vector<NS::Core::Matrix>& out) const;

        //! @brief 指定された姿勢から、各ボーンのワールド空間での変換行列を計算する
        //! @note アニメーションのリターゲットや、特定のボーンへのオブジェクトのアタッチに利用する
        void ComputeGlobals(std::span<const BonePose> pose,
                            std::vector<NS::Core::Matrix>& out,
                            bool applyRootTransform = true) const;

        //! 骨格全体にかかるルート変換を設定する
        void SetRootTransform(const NS::Core::Matrix& transform) noexcept;

        //! 骨格全体にかかるルート変換を返す
        [[nodiscard]] const NS::Core::Matrix& RootTransform() const noexcept;

        //! @brief CPU 側で頂点をスキニングして変形後の座標を出す
        //! @note 当たり判定の構築やデバッグ用途で使用する
        [[nodiscard]] static NS::Core::Vector3 SkinPositionReference(
            const SkinnedVertex& vertex, std::span<const NS::Core::Matrix> palette) noexcept;

    private:
        std::vector<Bone> m_bones;
        NS::Core::Matrix m_rootTransform{};
    };

} // namespace NS::Graphics
#pragma once

#include "Runtime/Graphics/SkeletalMesh.h"
#include "Runtime/Math/Math.h"

#include <span>
#include <string>

namespace NS::Graphics
{
    //! 1つのボーンの姿勢（位置・回転・スケール）を表すデータ
    struct BonePose
    {
        NS::Math::Vector3 translation{0.0f, 0.0f, 0.0f};
        NS::Math::Quaternion rotation{};
        NS::Math::Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    //! スケルトンを構成する1つのボーン構造
    struct Bone
    {
        int parentIndex = -1;           //!< 親ボーンのインデックス（ルートの場合は-1）
        NS::Math::Matrix inverseBind{}; //!< 初期姿勢の逆行列（バインドポーズ行列）
        BonePose bindLocal{};           //!< 初期姿勢（ローカル変換）
        std::string name;               //!< ボーン名（アニメーションの紐付けなどに使用する）
    };

    //! @brief キャラクターの骨格（スケルトン）を管理し、アニメーション用の行列を計算するクラス
    //! @details
    //! ボーンの親子関係を保持し、指定された姿勢（ポーズ）からシェーダへ渡すための行列配列（ボーンパレット）を生成する
    class Skeleton
    {
    public:
        Skeleton() = default;
        explicit Skeleton(std::vector<Bone> bones) noexcept;

        //! 管理しているボーンの総数を取得する
        [[nodiscard]] std::size_t BoneCount() const noexcept;
        [[nodiscard]] const std::vector<Bone>& Bones() const noexcept;

        //! @brief 指定された姿勢（ポーズ）から、シェーダ用のボーン行列配列を計算する
        //! @param pose 各ボーンのローカル姿勢（要素数は BoneCount() と一致すること）
        //! @param out 計算結果の出力先
        void ComputePalette(std::span<const BonePose> pose, std::vector<NS::Math::Matrix>& out) const;

        //! 初期姿勢（バインドポーズ）に基づくボーン行列配列を計算する
        void ComputeBindPalette(std::vector<NS::Math::Matrix>& out) const;

        //! @brief 指定された姿勢から、各ボーンのワールド空間での変換行列を計算する
        //! @note アニメーションのリターゲットや、特定のボーンへのオブジェクトのアタッチに利用する
        void ComputeGlobals(std::span<const BonePose> pose,
                            std::vector<NS::Math::Matrix>& out,
                            bool applyRootTransform = true) const;

        //! スケルトン全体に適用される根本（ルート）の変換行列を設定する
        void SetRootTransform(const NS::Math::Matrix& transform) noexcept;

        //! スケルトン全体に適用される根本（ルート）の変換行列を取得する
        [[nodiscard]] const NS::Math::Matrix& RootTransform() const noexcept;

        //! @brief CPU側で頂点のスキニング計算（変形後の座標計算）を行う補助関数
        //! @note 当たり判定の構築やデバッグ用途で使用する
        [[nodiscard]] static NS::Math::Vector3 SkinPositionReference(
            const SkinnedVertex& vertex, std::span<const NS::Math::Matrix> palette) noexcept;

    private:
        std::vector<Bone> m_bones;
        NS::Math::Matrix m_rootTransform{};
    };

} // namespace NS::Graphics
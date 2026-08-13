#pragma once

#include "Runtime/Graphics/Animation.h"
#include "Runtime/Graphics/MeshPrimitives.h"
#include "Runtime/Graphics/Skeleton.h"

#include <string>

namespace NS::Graphics
{
    //! @brief 指定された3Dモデルファイルを読み込み、単一のメッシュジオメトリとして展開する
    //! @details 全ノードのワールド変換を適用し、すべてのメッシュデータを連結する
    //! 右手系から左手系へ座標を変換し、法線を持たないデータには面法線を自動計算する
    //! 三角形以外の描画形状や圧縮されたデータには対応しない。読み込み失敗時はエラーログを出力し、空のデータを返す
    [[nodiscard]] MeshGeometry LoadGltfMesh(const std::string& path);

    //! スキニング対応モデルの読み込み結果
    struct SkinnedMeshData
    {
        std::vector<SkinnedVertex> vertices;   //!< スキニング用の頂点データ列
        std::vector<std::uint32_t> indices;    //!< 全プリミティブを連結したインデックス列
        Skeleton skeleton;                     //!< ボーン階層と逆バインド行列
        std::vector<AnimationClip> animations; //!< 読み込まれたアニメーションクリップ群

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !vertices.empty() && !indices.empty() && skeleton.BoneCount() > 0;
        }
    };

    //! @brief スキニング対応モデルを読み込む
    //! @details ボーンのインデックスやウェイト、スキン階層などのスキニング情報を付与する
    //! @return 読み込み失敗時は無効な状態のデータを返す
    [[nodiscard]] SkinnedMeshData LoadGltfSkinnedMesh(const std::string& path);

    //! @brief スキニング情報に依存しないアニメーションデータの読み込み結果
    //! @details メッシュやスキン情報を持たない、アニメーション専用のファイルからも読み込み可能
    struct AnimationSource
    {
        Skeleton skeleton;
        std::vector<AnimationClip> animations;

        [[nodiscard]] bool IsValid() const noexcept { return skeleton.BoneCount() > 0 && !animations.empty(); }
    };

    //! @brief スキニング情報に依存しないアニメーションを読み込む
    //! @return 読み込み失敗時は無効な状態のデータを返す
    [[nodiscard]] AnimationSource LoadGltfAnimationSource(const std::string& path);
} // namespace NS::Graphics
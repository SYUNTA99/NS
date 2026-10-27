#pragma once

#include "NSlib/Core/Types.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Core/Geometry/MeshPrimitives.h"
#include "NSlib/Core/Geometry/Vertex.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    //! 読み込み済みの glTF 1 件と、外部のファイルを読む方法
    //! ここの関数はファイルを開かない。.glb と埋め込みのバッファは bytes だけで読める
    //! .gltf が外部のバッファ (.bin など) を指す時は、baseDirectory に uri を繋いだ道で readFile を呼ぶ
    struct GltfFileDesc
    {
        std::span<const std::byte> bytes; // 本体の .gltf / .glb。読む間だけ生きていればよい
        std::string_view name;            // ログに出す名前
        std::string_view baseDirectory;   // 外部のバッファの uri を解く基準のフォルダ。空なら uri をそのまま渡す
        //! 道のファイルを全部読む。読めない時は nullopt を返し、読み込み全体が失敗する
        std::function<std::optional<std::vector<std::byte>>(std::string_view path)> readFile;
    };

    //! glTF を単一のメッシュジオメトリとして展開する
    //! 全ノードのワールド変換を適用し、すべてのメッシュデータを連結する
    //! 右手系から左手系へ座標を変換し、法線を持たないデータには面法線を自動計算する
    //! 三角形以外の描画形状や圧縮されたデータには対応しない。読み込み失敗時はエラーログを出力し、空のデータを返す
    [[nodiscard]] MeshGeometry ParseGltfMesh(const GltfFileDesc& file);

    //! スキニング対応モデルの読み込み結果
    struct SkinnedMeshData
    {
        std::vector<SkinnedVertex> vertices; // スキニング用の頂点データ列
        std::vector<uint32> indices;         // 全プリミティブを連結したインデックス列
        Anim::Skeleton skeleton;             // ボーン階層と逆バインド行列

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !vertices.empty() && !indices.empty() && skeleton.BoneCount() > 0;
        }
    };

    //! スキニング対応モデルを読む
    //! ボーンのインデックスやウェイト、スキン階層などのスキニング情報を付与する
    //! @return 読み込み失敗時は無効な状態のデータを返す
    [[nodiscard]] SkinnedMeshData ParseGltfSkinnedMesh(const GltfFileDesc& file);

    //! スキニング情報に依存しないアニメーションデータの読み込み結果
    //! メッシュやスキン情報を持たない、アニメーション専用のファイルからも読み込み可能
    struct AnimSource
    {
        Anim::Skeleton skeleton;
        std::vector<Anim::AnimClip> animations;

        [[nodiscard]] bool IsValid() const noexcept { return skeleton.BoneCount() > 0 && !animations.empty(); }
    };

    //! スキニング情報に依存しないアニメーションを読む
    //! @return 読み込み失敗時は無効な状態のデータを返す
    [[nodiscard]] AnimSource ParseGltfAnimationSource(const GltfFileDesc& file);
} // namespace NS

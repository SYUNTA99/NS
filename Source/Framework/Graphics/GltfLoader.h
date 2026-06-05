#pragma once

/// @file GltfLoader.h
/// @brief glTF 2.0 ファイルから static / skinned ジオメトリを読み込む (cgltf ベース)
///
/// @details 全 node の world 変換を適用しつつ、 全 mesh・全 primitive の POSITION / TEXCOORD_0 / NORMAL /
/// index を 1 つの MeshGeometry に連結する (primitive ごとに baseVertex offset を付与)
/// 座標系は glTF (右手 Y-up、 front=CCW) から NS (左手、 front=CW) へ position / normal の Z 反転 +
/// 三角形 winding 反転で変換し、 NORMAL が無い primitive は面法線から smooth normal を自前計算する
/// 三角形以外の topology と Draco 圧縮 primitive は skip / 拒否する
/// skinned 取込は JOINTS_0 / WEIGHTS_0 / skin 階層 / inverse bind を加えて SkinnedMeshData を返す
/// 複数 Material は後続、 cgltf 依存は実装 (.cpp) に閉じるため本ヘッダは公開して良い

#include "Framework/Graphics/Animation.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Skeleton.h"

#include <cstdint>
#include <string>
#include <vector>

namespace NS::Graphics
{
    /// glTF (.gltf / .glb) を読み MeshGeometry に展開する
    /// ファイル IO は NS::Core::FileSystem 経由、 parse は cgltf (メモリ上で実行)
    /// 失敗 (ファイル無し / parse 失敗 / POSITION 欠如) 時は empty を返し NS_LOG_ERROR を出す
    /// 戻り値が empty かどうかで成否を判定できる (vertices が空 = 失敗)
    [[nodiscard]] MeshGeometry LoadGltfMesh(const std::string& path);

    /// skinned glTF の取込結果。 skinned 頂点・index と Skeleton を保持する
    struct SkinnedMeshData
    {
        std::vector<SkinnedVertex> vertices;
        std::vector<std::uint32_t> indices;
        Skeleton skeleton;
        std::vector<AnimationClip> animations; // 取込めたクリップ (無い glTF では空)

        /// 頂点・index・ボーンが揃っていれば true。 失敗時は空で false (animations は任意)
        [[nodiscard]] bool IsValid() const noexcept
        {
            return !vertices.empty() && !indices.empty() && skeleton.BoneCount() > 0;
        }
    };

    /// skinned glTF (.gltf / .glb) を読み SkinnedMeshData に展開する
    /// JOINTS_0 / WEIGHTS_0 / skin の joint 階層 / inverse bind を取り込み、 既存 static と同じ左手座標へ変換する
    /// (頂点・joint 変換とも S=diag(1,1,-1) で揃え、 skinned mesh node の node 変換は焼き込まない)
    /// skin 無し / joint index 範囲外 / inverse bind 欠落 / ボーン上限超過 / 非三角形 / Draco は
    /// NS_LOG_ERROR の上 empty を返す (IsValid() == false で検知)
    [[nodiscard]] SkinnedMeshData LoadGltfSkinnedMesh(const std::string& path);
} // namespace NS::Graphics

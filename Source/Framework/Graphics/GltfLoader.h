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
    /// glTF (.gltf / .glb) を読み MeshGeometry に展開する。失敗時は empty を返し NS_LOG_ERROR を出す
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

    /// skinned glTF を読み SkinnedMeshData に展開する。失敗時は empty (IsValid()==false) を返す
    [[nodiscard]] SkinnedMeshData LoadGltfSkinnedMesh(const std::string& path);

    /// skin 非依存で読んだアニメソース。 リターゲット元の骨格 (rest + 骨名) と clips を保持する
    /// mesh / skin / inverseBind が無い (アニメのみの) glTF でも読める
    struct AnimationSource
    {
        Skeleton skeleton;                     // rest(bindLocal) + 骨名 + rootTransform。 inverseBind は恒等 (未使用)
        std::vector<AnimationClip> animations; // tracks は source 骨 index

        /// 骨格と animation が揃っていれば true
        [[nodiscard]] bool IsValid() const noexcept { return skeleton.BoneCount() > 0 && !animations.empty(); }
    };

    /// skin 非依存でアニメ glTF を読み AnimationSource を返す。失敗時は IsValid()==false
    [[nodiscard]] AnimationSource LoadGltfAnimationSource(const std::string& path);
} // namespace NS::Graphics

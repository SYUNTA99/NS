#pragma once

/// @file StaticMesh.h
/// @brief NS::Graphics::StaticMesh — 固定頂点フォーマット (StaticVertex) の静的メッシュ
///
/// @details 基底 Mesh の派生。 `StaticVertex` (32 byte: position/uv/normal) を VB、 uint16 を IB に持つ
/// 構築した buffer を基底 `SetGeometry` に預け、 `Draw` / `IsValid` 等は基底実装を共有する
/// MeshDesc 不正 / Buffer 失敗時は fallback Cube に切替わる (基底 IsUsingFallback で検知)
/// Submesh / 複数 Material は glTF 対応時に拡張、 cube は単一マテリアル相当
/// @pre Renderer の DeviceContext を内部保持するため Renderer より先に破棄すること

#include "Framework/Graphics/Mesh.h"
#include "Framework/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace NS::Graphics
{
    class Renderer;

    /// 固定頂点フォーマット (32 byte 固定)
    /// cube と glTF static の共通形式。 ボーン重み付きの SkinnedVertex は SkeletalMesh が別途持つ
    struct StaticVertex
    {
        NS::Math::Vector3 position;
        NS::Math::Vector2 uv;
        NS::Math::Vector3 normal;
    };
    static_assert(sizeof(StaticVertex) == 32, "StaticVertex は 32 byte 固定");
    static_assert(std::is_standard_layout_v<StaticVertex>,
                  "StaticVertex は offsetof 使用のため標準レイアウト必須 (StandardInputLayout)");

    /// Mesh 構築パラメータ。 Static Buffer 前提で initialData はコンストラクタ内でコピーされる
    /// Index は uint32_t (大きい glTF モデルの 65535 vertex 超に対応)
    struct MeshDesc
    {
        const StaticVertex* vertices = nullptr;
        std::size_t vertexCount = 0;
        const std::uint32_t* indices = nullptr;
        std::size_t indexCount = 0;
    };

    /// 固定頂点フォーマット (StaticVertex) の静的メッシュ。 MeshDesc 不正 / Buffer 失敗時は
    /// fallback Cube に切替わる (基底 IsUsingFallback で検知)。 Submesh / 複数 Material は glTF 対応時に拡張
    class StaticMesh : public Mesh
    {
    public:
        StaticMesh(Renderer& renderer, const MeshDesc& desc);
        ~StaticMesh() override = default;

        StaticMesh(const StaticMesh&) = delete;
        StaticMesh& operator=(const StaticMesh&) = delete;
        StaticMesh(StaticMesh&&) = delete;
        StaticMesh& operator=(StaticMesh&&) = delete;

        /// StaticVertex に対応する POSITION / TEXCOORD / NORMAL の InputElement 配列を返す
        /// 基底が CreateInputLayout で使う頂点レイアウトと同一
        [[nodiscard]] static std::vector<InputElement> StandardInputLayout();
    };

} // namespace NS::Graphics

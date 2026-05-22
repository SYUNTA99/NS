#pragma once

/// @file mesh_component.h
/// @brief MeshComponent — Mesh + Material 描画を担う IRenderable 多重継承 Component (, )。
///
/// 既存 standard.{vs,ps}.hlsl + FrameCB 構造 (160 byte, row_major LH) を流用する。
/// `Draw(ctx)` 内で `Transform::InterpolatedWorldMatrix(ctx.alpha)` を使い、
/// fixed step 物理結果を可変 frame rate で smooth 補間描画する (Glenn Fiedler accumulator)。

#include "Framework/Core/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/IRenderable.h"

namespace NS::Graphics
{
    class Mesh;
    class Material;
} // namespace NS::Graphics

namespace NS::Scene
{
    /// HLSL standard FrameCB と完全一致 (sizeof=160、16 byte 倍数)。
    /// Material::SetParams に渡す per-draw constant buffer。
    struct alignas(16) FrameCB
    {
        NS::Core::Matrix world{};
        NS::Core::Matrix viewProj{};
        NS::Core::Vector3 lightDir{-0.3f, -1.0f, -0.2f};
        float pad0 = 0.0f;
        NS::Core::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        float pad1 = 0.0f;
    };
    static_assert(sizeof(FrameCB) == 160, "FrameCB size は HLSL standard と完全一致 (160 byte)");
    static_assert(alignof(FrameCB) == 16, "FrameCB は 16 byte alignment ()");

    class MeshComponent : public Component, public IRenderable
    {
    public:
        /// Mesh / Material は raw pointer、寿命は呼出側 (通常は Scene or Player) が保証する。
        MeshComponent(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material) noexcept;

        /// 光源方向 (default は CubeScene と同値 (-0.3, -1, -0.2) を normalize 前で渡す)。
        void SetLightDirection(const NS::Core::Vector3& dir) noexcept { m_lightDir = dir; }
        /// Material instance ごとの色味 (Player=赤系 / Block=灰色系の色分け)。
        void SetBaseColor(const NS::Core::Vector3& color) noexcept { m_baseColor = color; }

        /// IRenderable: Alpha 補間後の world matrix を FrameCB に詰めて 1 描画呼出。
        /// IsActive() == false なら no-op。
        void Draw(const RenderContext& context) override;

        /// Owner の OwningScene に self を IRenderable として登録する。
        /// Owner / OwningScene が null の時は no-op で安全に return する。
        void OnStart() override;
        /// Owner の OwningScene から self を解除する。RootScene 破棄前に呼ぶことで
        /// dangling pointer を残さない。Owner / OwningScene が null の時は no-op。
        void OnEndPlay() override;

    private:
        NS::Graphics::Mesh* m_mesh = nullptr;
        NS::Graphics::Material* m_material = nullptr;
        NS::Core::Vector3 m_lightDir{-0.3f, -1.0f, -0.2f};
        NS::Core::Vector3 m_baseColor{1.0f, 1.0f, 1.0f};
    };
} // namespace NS::Scene

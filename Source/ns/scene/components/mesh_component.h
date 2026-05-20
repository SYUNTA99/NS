#pragma once

/// @file mesh_component.h
/// @brief MeshComponent — Mesh + Material 描画を担う IRenderable 多重継承 Component (, )。
///
/// 既存 standard.{vs,ps}.hlsl + FrameCB 構造 (160 byte, row_major LH) を流用する。
/// `Draw(ctx)` 内で `Transform::InterpolatedWorldMatrix(ctx.alpha)` を使い、
/// fixed step 物理結果を可変 frame rate で smooth 補間描画する (Glenn Fiedler accumulator)。

#include "ns/core/math.h"
#include "ns/scene/component.h"
#include "ns/scene/i_renderable.h"

namespace ns::graphics
{
    class Mesh;
    class Material;
} // namespace ns::graphics

namespace ns::scene
{
    /// HLSL standard FrameCB と完全一致 (sizeof=160、16 byte 倍数)。
    /// Material::SetParams に渡す per-draw constant buffer。
    struct alignas(16) FrameCB
    {
        ns::core::Matrix world{};
        ns::core::Matrix viewProj{};
        ns::core::Vector3 lightDir{-0.3f, -1.0f, -0.2f};
        float pad0 = 0.0f;
        ns::core::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        float pad1 = 0.0f;
    };
    static_assert(sizeof(FrameCB) % 16 == 0, "FrameCB は 16 byte 倍数 ()");

    class MeshComponent : public Component, public IRenderable
    {
    public:
        /// Mesh / Material は raw pointer、寿命は呼出側 (通常は Scene or Player) が保証する。
        MeshComponent(ns::graphics::Mesh* mesh, ns::graphics::Material* material) noexcept;

        /// 光源方向 (default は CubeScene と同値 (-0.3, -1, -0.2) を normalize 前で渡す)。
        void SetLightDirection(const ns::core::Vector3& dir) noexcept { m_lightDir = dir; }
        /// Material instance ごとの色味 (Player=赤系 / Block=灰色系の色分け)。
        void SetBaseColor(const ns::core::Vector3& color) noexcept { m_baseColor = color; }

        /// IRenderable: Alpha 補間後の world matrix を FrameCB に詰めて 1 描画呼出。
        /// IsActive() == false なら no-op。
        void Draw(const RenderContext& context) override;

    private:
        ns::graphics::Mesh* m_mesh = nullptr;
        ns::graphics::Material* m_material = nullptr;
        ns::core::Vector3 m_lightDir{-0.3f, -1.0f, -0.2f};
        ns::core::Vector3 m_baseColor{1.0f, 1.0f, 1.0f};
    };
} // namespace ns::scene

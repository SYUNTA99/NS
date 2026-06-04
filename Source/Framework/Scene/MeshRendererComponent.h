#pragma once

/// @file MeshRendererComponent.h
/// @brief MeshRendererComponent — Mesh + Material 描画を担う IRenderable 多重継承 Component
///
/// 既存 standard.{vs,ps}.hlsl + FrameCB 構造 (160 byte, row_major LH) を流用する
/// `Draw(context)` 内で `Transform::InterpolatedWorldMatrix(context.alpha)` を使い、
/// fixed step 物理結果を可変 frame rate でなめらかに補間描画する

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/IRenderable.h"

namespace NS::Graphics
{
    class Mesh;
    class Material;
} // namespace NS::Graphics

namespace NS::Scene
{
    /// HLSL standard FrameCB と完全一致 (sizeof=192、 16 byte 倍数)
    /// Material::SetParams に渡す per-draw constant buffer
    /// lightColor / ambientColor は ThemeRegistry::Get(level.themeId) から毎フレーム流し込む
    struct alignas(16) FrameCB
    {
        NS::Math::Matrix world{};
        NS::Math::Matrix viewProj{};
        NS::Math::Vector3 lightDir{-0.3f, -1.0f, -0.2f};
        float pad0 = 0.0f;
        NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        float pad1 = 0.0f;
        NS::Math::Vector3 lightColor{1.0f, 1.0f, 1.0f};
        float pad2 = 0.0f;
        NS::Math::Vector3 ambientColor{0.2f, 0.2f, 0.2f};
        float pad3 = 0.0f;
    };
    static_assert(sizeof(FrameCB) == 192, "FrameCB size は HLSL standard と完全一致 (192 byte)");
    static_assert(alignof(FrameCB) == 16, "FrameCB は 16 byte alignment");

    class MeshRendererComponent : public Component, public IRenderable
    {
    public:
        /// GameObject owner と Mesh / Material を同時に受け取って auto-register するコンストラクタ
        /// Mesh / Material は raw pointer、寿命は呼出側 (通常は Scene or Player) が保証する
        MeshRendererComponent(NS::Scene::GameObject* owner,
                              NS::Graphics::Mesh* mesh,
                              NS::Graphics::Material* material) noexcept;

        /// 光源方向 (default は CubeScene と同値 (-0.3, -1, -0.2) を normalize 前で渡す)
        void SetLightDirection(const NS::Math::Vector3& dir) noexcept { m_lightDir = dir; }
        /// Material instance ごとの色味 (Player=赤系 / Block=灰色系の色分け)
        void SetBaseColor(const NS::Math::Vector3& color) noexcept { m_baseColor = color; }
        /// テーマ駆動 sun color (ThemeData::lightColor)。 LevelEditorScene が毎フレーム流す
        void SetLightColor(const NS::Math::Vector3& color) noexcept { m_lightColor = color; }
        /// テーマ駆動 ambient color (ThemeData::ambientColor)。 LevelEditorScene が毎フレーム流す
        void SetAmbientColor(const NS::Math::Vector3& color) noexcept { m_ambientColor = color; }

        /// IRenderable: Alpha 補間後の world matrix を FrameCB に詰めて 1 描画呼出
        /// IsActive() == false なら no-op
        void Draw(const RenderContext& context) override;

        /// Owner の OwningScene に self を IRenderable として登録する
        /// Owner / OwningScene が null の時は no-op で安全に return する
        void OnStart() override;
        /// Owner の OwningScene から self を解除する。SceneBase 破棄前に呼ぶことで
        /// dangling pointer を残さない。Owner / OwningScene が null の時は no-op
        void OnEndPlay() override;

    private:
        NS::Graphics::Mesh* m_mesh = nullptr;
        NS::Graphics::Material* m_material = nullptr;
        NS::Math::Vector3 m_lightDir{-0.3f, -1.0f, -0.2f};
        NS::Math::Vector3 m_baseColor{1.0f, 1.0f, 1.0f};
        NS::Math::Vector3 m_lightColor{1.0f, 1.0f, 1.0f};
        NS::Math::Vector3 m_ambientColor{0.2f, 0.2f, 0.2f};
    };
} // namespace NS::Scene

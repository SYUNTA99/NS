#pragma once

/// @file MeshRendererComponent.h
/// @brief MeshRendererComponent — Mesh + Material 描画を担う IRenderable 多重継承 Component
///
/// 既存 standard.{vs,ps}.hlsl + FrameCB 構造 (192 byte, row_major LH) を流用する
/// `Draw(context)` 内で `Transform::InterpolatedWorldMatrix(context.alpha)` を使い、
/// fixed step 物理結果を可変 frame rate でなめらかに補間描画する

#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/IRenderable.h"

#include <string>
#include <utility>

namespace NS::Graphics
{
    class Mesh;
    class Material;
} // namespace NS::Graphics

namespace NS::Scene
{
    /// per-draw constant buffer。HLSL standard と完全一致 (sizeof=192)
    struct alignas(16) FrameCB
    {
        NS::Math::Matrix world{};
        NS::Math::Matrix viewProj{};
        // lighting の既定値はプロジェクト描画既定値 (RenderSettings) と共有し、値の二重管理を避ける
        NS::Math::Vector3 lightDir = NS::Graphics::RenderSettings{}.lightDir;
        float pad0 = 0.0f;
        NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        float pad1 = 0.0f;
        NS::Math::Vector3 lightColor = NS::Graphics::RenderSettings{}.lightColor;
        float pad2 = 0.0f;
        NS::Math::Vector3 ambientColor = NS::Graphics::RenderSettings{}.ambientColor;
        float pad3 = 0.0f;
    };
    static_assert(sizeof(FrameCB) == 192, "FrameCB size は HLSL standard と完全一致 (192 byte)");
    static_assert(alignof(FrameCB) == 16, "FrameCB は 16 byte alignment");

    class MeshRendererComponent : public Component, public IRenderable
    {
    public:
        /// Mesh / Material は生ポインタ、寿命は呼出側 (通常は Scene or Player) が保証する
        MeshRendererComponent(NS::Graphics::Mesh* mesh, NS::Graphics::Material* material) noexcept;

        /// Material instance ごとの色味 (Player=赤系 / Block=灰色系の色分け)。lighting とは別系統の個体色
        void SetBaseColor(const NS::Math::Vector3& color) noexcept { m_baseColor = color; }

        /// 描画に使う Material を差し替える。material=nullptr で Draw は何もしなくなる
        /// bucket は Draw 時に Material::Blend() から都度判定するため opaque↔transparent も即反映される
        void SetMaterial(NS::Graphics::Material* material) noexcept { m_material = material; }
        /// 現在の Material (非所有)。未設定なら nullptr
        [[nodiscard]] NS::Graphics::Material* GetMaterial() const noexcept { return m_material; }

        /// 描画に使う Mesh を差し替える。mesh=nullptr で Draw は何もしなくなる
        /// 反射では mesh を運べないため、 components 駆動の構築側が kind から引いた geometry をここで当てる
        void SetMesh(NS::Graphics::Mesh* mesh) noexcept { m_mesh = mesh; }
        /// build 時に解決された実体 Mesh を返す。 未解決なら nullptr
        [[nodiscard]] const NS::Graphics::Mesh* GetMesh() const noexcept { return m_mesh; }

        /// 描くメッシュの参照。 builtin 名または ContentRoot 配下の相対パス。 空なら build が kind から解決する
        [[nodiscard]] const std::string& MeshRef() const noexcept { return m_meshRef; }
        /// 描くメッシュの参照を設定する。 build 時にこの文字列から mesh を解決する
        void SetMeshRef(std::string ref) noexcept { m_meshRef = std::move(ref); }

        /// 描画 material の参照。 共有 material 名 (player / block / water / shadow) または .mat 相対パス
        /// 空なら build が materialIndex / 既定 material へ倒す
        [[nodiscard]] const std::string& MaterialRef() const noexcept { return m_materialRef; }
        /// 描画 material の参照を設定する。 build 時にこの文字列から material を解決する
        void SetMaterialRef(std::string ref) noexcept { m_materialRef = std::move(ref); }

        /// 個体段の lighting 上書き。空なら scene 解決値 (ctx.resolvedSettings) がそのまま使われる
        /// 描画時に Resolve(ctx.resolvedSettings, m_objectOverride) で個体段を解決する
        void SetRenderOverride(const NS::Graphics::RenderSettingsOverride& over) noexcept { m_objectOverride = over; }
        /// 現在の個体段 override を返す (出所表示・編集用)
        [[nodiscard]] const NS::Graphics::RenderSettingsOverride& RenderOverride() const noexcept
        {
            return m_objectOverride;
        }

        /// alpha 補間 world matrix を FrameCB に詰めて 1 描画呼出。IsActive()==false なら何もしない
        /// 描画直前に Material の BlendMode に対応する共通 Pipeline を SetPipeline する (state リーク防止)
        void Draw(const RenderContext& context) override;

        /// Material の BlendMode から bucket を返す (Opaque 以外は Transparent)。Material 不在は Opaque
        [[nodiscard]] RenderBucket Bucket() const noexcept override;
        /// Owner の world 行列の平行移動成分 (半透明ソート用中心)
        [[nodiscard]] NS::Math::Vector3 SortCenter() const noexcept override;
        /// Material の renderPriority (距離同値時のタイブレーク)
        [[nodiscard]] int SortPriority() const noexcept override;

        /// OwningScene に self を IRenderable として登録する。Owner/Scene が null なら何もしない
        void OnStart() override;
        /// Owner の OwningScene から self を解除する。無効ポインタを残さないよう SceneBase 破棄前に呼ぶ
        void OnEndPlay() override;

        // 個体色とメッシュ / material 参照を Inspector へ公開する。 lighting とは別系統の個体色 + 描く住み処
        NS_REFLECT_BEGIN(MeshRendererComponent)
        NS_REFLECT_FIELD(m_baseColor, "Base Color")
        NS_REFLECT_FIELD(m_meshRef, "Mesh")
        NS_REFLECT_FIELD(m_materialRef, "Material")
        NS_REFLECT_END()

    private:
        NS::Graphics::Mesh* m_mesh = nullptr;
        NS::Graphics::Material* m_material = nullptr;
        NS::Math::Vector3 m_baseColor{1.0f, 1.0f, 1.0f};
        // 保存・編集される参照文字列。 build 時に解決して m_mesh / m_material へ実体を当てる二層構造
        std::string m_meshRef{};
        std::string m_materialRef{};
        NS::Graphics::RenderSettingsOverride m_objectOverride{};
    };
} // namespace NS::Scene

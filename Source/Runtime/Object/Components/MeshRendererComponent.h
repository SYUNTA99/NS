#pragma once

#include "Runtime/Graphics/DrawItem.h"
#include "Runtime/Graphics/RenderSettings.h"
#include "Runtime/Math/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/IRenderable.h"

#include <string>
#include <utility>
#include <vector>

namespace NS::Graphics
{
    class Mesh;
    class Material;
    class Buffer;
} // namespace NS::Graphics

namespace NS::Object
{
    /// @brief Mesh + Material 描画を担う IRenderable 多重継承 Component
    /// @details 既存 standard.{vs,ps}.hlsl + FrameCB 構造を流用し、FrameCB は 192 byte で row_major LH
    /// Collect で Transform::InterpolatedWorldMatrix(context.alpha) を FrameCB に詰めた DrawItem を積む
    /// fixed step 物理結果を可変 frame rate でなめらかに補間描画する
    class MeshRendererComponent : public Component, public IRenderable
    {
    public:
        /// Mesh / Material は非所有の生ポインタ。空で作り、後から SetMesh / SetMaterial で入れる
        /// 寿命は AssetManager 等の所有側が保証する
        MeshRendererComponent() noexcept = default;

        /// Material instance ごとの色味で Player は赤系 Block は灰色系に色分けする。lighting とは別系統の個体色
        void SetBaseColor(const NS::Math::Vector3& color) noexcept { m_baseColor = color; }

        /// 描画に使う Material を差し替える。material=nullptr で Draw は何もしなくなる
        /// bucket は Draw 時に Material::Blend() から都度判定するため opaque↔transparent も即反映される
        void SetMaterial(NS::Graphics::Material* material) noexcept { m_material = material; }
        /// 現在の Material で非所有。未設定なら nullptr
        [[nodiscard]] NS::Graphics::Material* GetMaterial() const noexcept { return m_material; }

        /// 描画に使う Mesh を差し替える。mesh=nullptr で Draw は何もしなくなる
        /// リフレクションでは Mesh を運べないので、 MeshRef から解決したものをここで差す
        void SetMesh(NS::Graphics::Mesh* mesh) noexcept { m_mesh = mesh; }
        /// build 時に解決された実体 Mesh を返す。 未解決なら nullptr
        [[nodiscard]] const NS::Graphics::Mesh* GetMesh() const noexcept { return m_mesh; }

        /// 描くメッシュの参照。 builtin 名または ContentRoot 配下の相対パス。 空 / 解決不可なら構築側が既定にする
        [[nodiscard]] const std::string& MeshRef() const noexcept { return m_meshRef; }
        /// build 時にこの文字列から mesh を解決する
        void SetMeshRef(std::string ref) noexcept { m_meshRef = std::move(ref); }

        /// 描画 material の参照。 player / water / shadow といった共有 material 名または .mat 相対パス
        /// 空・解決不可は ResolveAssets が既定 material にする
        [[nodiscard]] const std::string& MaterialRef() const noexcept { return m_materialRef; }
        /// build 時にこの文字列から material を解決する
        void SetMaterialRef(std::string ref) noexcept { m_materialRef = std::move(ref); }

        /// 個体段の lighting 上書き。空なら scene 解決値の ctx.resolvedSettings がそのまま使われる
        /// 描画時に Resolve(ctx.resolvedSettings, m_objectOverride) で個体段を解決する
        void SetRenderOverride(const NS::Graphics::RenderSettingsOverride& over) noexcept { m_objectOverride = over; }
        /// 出所表示や編集に使う
        [[nodiscard]] const NS::Graphics::RenderSettingsOverride& RenderOverride() const noexcept
        {
            return m_objectOverride;
        }

        /// skinned のボーンパレット等、オブジェクト単位の追加 VS 定数を差す。同じ object 上の別 component が OnStart で配線する
        /// cpuData 非 null なら描画側が毎描画 buffer へ upload してから bind する
        void SetPerObjectVsConstant(const NS::Graphics::Buffer* cb,
                                    const void* cpuData,
                                    std::size_t cpuDataSize,
                                    unsigned slot) noexcept;

        /// skinned など mesh 固定の LocalBounds では現在ポーズを包めない時に、同じ object の component が毎フレーム
        /// 現在ポーズの局所境界を差す。差された間は WorldBounds がこれを world 変換して使う
        void SetLocalBoundsOverride(const NS::Math::AABB& localBounds) noexcept
        {
            m_localBoundsOverride = localBounds;
            m_hasLocalBoundsOverride = true;
        }

        /// alpha 補間 world matrix を FrameCB に詰めた DrawItem を out に積む。IsActive()==false なら何も積まない
        void Collect(const NS::Graphics::RenderContext& context, std::vector<NS::Graphics::DrawItem>& out) override;

        /// Material の BlendMode から bucket を返し、 Opaque 以外は Transparent。Material 不在は Opaque
        [[nodiscard]] RenderBucket Bucket() const noexcept override;
        /// Owner の world 行列の平行移動成分で半透明ソート用の中心
        [[nodiscard]] NS::Math::Vector3 SortCenter() const noexcept override;
        /// Material の renderPriority で距離同値時のタイブレークに使う
        [[nodiscard]] int SortPriority() const noexcept override;

        /// mesh の局所 AABB を owner の world 行列で包んだワールド AABB。mesh / owner 不在なら原点の点
        [[nodiscard]] NS::Math::AABB WorldBounds() const noexcept override;

        /// OwningScene に self を IRenderable として登録する。Owner/Scene が null なら何もしない
        void OnStart() override;
        /// Owner の OwningScene から self を解除する。無効ポインタを残さないよう Scene 破棄前に呼ぶ
        void OnEndPlay() override;

        /// meshRef / matRef の参照文字列から実体の Mesh / Material を引き当てる
        /// 共有 material 名を先に引き、 外れたら .mat 相対パスとして読む。 解決不可は cube と既定 material にする
        void ResolveAssets(AssetManager& assets) override;

        // lighting とは別系統の個体色 + 描画の登録先
        NS_REFLECT_BEGIN(MeshRendererComponent, Component)
        NS_REFLECT_FIELD(m_baseColor, "Base Color")
        NS_REFLECT_FIELD(m_meshRef, "Mesh")
        NS_REFLECT_FIELD(m_materialRef, "Material")
        NS_REFLECT_END()

    private:
        NS::Graphics::Mesh* m_mesh = nullptr;            // 描画する Mesh (非所有)
        NS::Graphics::Material* m_material = nullptr;    // 描画に使う Material (非所有)
        NS::Math::Vector3 m_baseColor{1.0f, 1.0f, 1.0f}; // 個体色、lighting と別系統
        // 保存・編集される参照文字列。 build 時に解決して m_mesh / m_material へ実体を当てる二層構造
        std::string m_meshRef{};
        std::string m_materialRef{};
        NS::Graphics::RenderSettingsOverride m_objectOverride{}; // 個体段の lighting 上書き

        // オブジェクト単位の追加 VS 定数 (skinned のボーンパレット)。同じ object 上の別 component が SetPerObjectVsConstant で差す
        const NS::Graphics::Buffer* m_perObjectVsCb = nullptr;
        const void* m_perObjectVsData = nullptr;
        std::size_t m_perObjectVsSize = 0;
        unsigned m_perObjectVsSlot = 1;

        // skinned の現在ポーズ境界。同じ object の SkeletalAnimationComponent が毎フレーム差す
        NS::Math::AABB m_localBoundsOverride{};
        bool m_hasLocalBoundsOverride = false;
    };
} // namespace NS::Object

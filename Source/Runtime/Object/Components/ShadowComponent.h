#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/IRenderable.h"

#include <vector>

namespace NS::Graphics
{
    class StaticMesh;
    class Material;
} // namespace NS::Graphics

namespace NS::Object
{
    //! @brief owner の真下の地面に半透明の接地シャドウを描く IRenderable Component
    //! @details 下方向レイ vs 所属 Scene の衝突形状で真下の地面を探し、命中面に水平な半透明クアッドを置く
    //! 高さに応じて薄く・小さくし、空中でも着地点が分かる目印にする。真下に地面が無ければ描かない
    //! Alpha 合成で深度テストON 書込OFF の半透明バケットで描かれ、手前のジオメトリに遮蔽される
    //! 受け先は毎 Collect で PhysicsWorld を引き直す。落下先を控えないので、当たりを張り直した後の位置に影が乗る
    //! quad mesh / shadow material は AssetManager の共有物を ResolveAssets が差す。本 Component は非所有
    class ShadowComponent : public Component, public IRenderable
    {
    public:
        //! 共有の quad mesh と shadow material を非所有で注入する。未設定なら Collect は何も積まない
        void SetResources(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept;

        //! 真下の地面に影クアッドの DrawItem を積む。地面が無い / リソース未設定なら何も積まない
        void Collect(const NS::Graphics::RenderContext& context, std::vector<NS::Graphics::DrawItem>& out) override;
        //! 半透明バケットに分類させる
        [[nodiscard]] RenderBucket Bucket() const noexcept override { return RenderBucket::Transparent; }
        //! 半透明ソート用の中心で owner world 位置を返す
        [[nodiscard]] NS::Core::Vector3 SortCenter() const noexcept override;

        //! owner 直下の落下可動域を丸ごと覆う AABB。影がどこに落ちても視錐台判定で外さない
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override;

        //! OwningScene に自分を IRenderable として登録する
        void OnStart() override;
        //! OwningScene から自分を解除する
        void OnEndPlay() override;

        //! 共有の quad mesh と shadow material を引き当てる。 参照文字列は持たず資材は固定
        void ResolveAssets(AssetManager& assets) override;

        //! 落下距離 dist に対する高さフェード係数 [0,1]。dist=0 で 1、dist>=maxDist で 0
        [[nodiscard]] static float ComputeFade(float dist, float maxDist) noexcept;

        // 接地シャドウの見た目を Inspector へ公開する。 毎 Draw 読まれるのでライブで効く
        NS_REFLECT_BEGIN(ShadowComponent, Component)
        NS_REFLECT_FIELD(m_baseDiameter, "基本直径")
        NS_REFLECT_FIELD(m_maxDrop, "最大投影距離")
        NS_REFLECT_FIELD(m_surfaceOffset, "表面オフセット")
        NS_REFLECT_FIELD(m_baseAlpha, "基本不透明度")
        NS_REFLECT_END()

    private:
        NS::Graphics::StaticMesh* m_mesh = nullptr;   // 共有 quad mesh (非所有)
        NS::Graphics::Material* m_material = nullptr; // 共有 shadow material (非所有)

        float m_baseDiameter = 1.2f;   // 接地時の影の直径
        float m_maxDrop = 12.0f;       // 影が消える最大落下距離
        float m_surfaceOffset = 0.02f; // 地面へめり込ませない浮かせ量
        float m_baseAlpha = 0.5f;      // 接地時の不透明度
    };
} // namespace NS::Object

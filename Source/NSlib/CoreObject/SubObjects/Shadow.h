#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/SubObject.h"

#include <vector>

namespace NS
{
    namespace Gfx
    {
        class Mesh;
    }
    namespace Gfx
    {
        class Material;
    }
} // namespace NS

namespace NS
{
    //! 真下の地形へ丸影を描く。地面がなければ描かず、高さに応じて薄く小さくする
    //! 位置は描画時に衝突へ問い合わせ、過去の命中位置を保持しない
    class Shadow : public SubObject, public IRenderable
    {
    public:
        //! 真下の地面に影クアッドの DrawItem を積む。地面が無い / リソース未設定なら何も積まない
        void Collect(const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out) override;
        [[nodiscard]] RenderBucket Bucket() const noexcept override { return RenderBucket::Transparent; }
        //! 半透明の並べ替えに使う中心として、持ち主の世界座標の位置を返す
        [[nodiscard]] Vector3 SortCenter() const noexcept override;

        //! owner 直下の落下可動域を丸ごと覆う AABB。影がどこに落ちても視錐台判定で外さない
        [[nodiscard]] AABB WorldBounds() const noexcept override;

        //! OwningScene に自分を IRenderable として登録する
        void Register();
        //! OwningScene から自分を解除する
        void Unregister();

        //! 落下距離 dist に対する高さフェード係数 [0,1]。dist=0 で 1、dist>=maxDist で 0
        [[nodiscard]] static float ComputeFade(float dist, float maxDist) noexcept;

        // 接地シャドウの見た目を Inspector へ公開する。毎 Draw 読まれるのでライブで効く
        NS_REFLECT_BEGIN(Shadow, SubObject)
        NS_REFLECT_FIELD(m_mesh, "メッシュ")
        NS_REFLECT_FIELD(m_material, "マテリアル")
        NS_REFLECT_FIELD(m_baseDiameter, "基本直径")
        NS_REFLECT_FIELD(m_maxDrop, "最大投影距離")
        NS_REFLECT_FIELD(m_surfaceOffset, "表面オフセット")
        NS_REFLECT_FIELD(m_baseAlpha, "基本不透明度")
        NS_REFLECT_END()

    private:
        AssetRef<Gfx::Mesh> m_mesh{"shadowQuad"};     // 影を描く板
        AssetRef<Gfx::Material> m_material{"shadow"}; // 影のマテリアル

        float m_baseDiameter = 1.2f;   // 接地時の影の直径
        float m_maxDrop = 12.0f;       // 影が消える最大落下距離
        float m_surfaceOffset = 0.02f; // 地面へめり込ませない浮かせ量
        float m_baseAlpha = 0.5f;      // 接地時の不透明度
    };
} // namespace NS

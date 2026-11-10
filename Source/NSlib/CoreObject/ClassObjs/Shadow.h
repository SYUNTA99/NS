#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Gfx/Render/DrawItem.h"
#include "NSlib/Gfx/Render/RenderBounds.h"

#include <optional>
#include <vector>

namespace NS
{
    class IGetCollision;
    namespace Gfx
    {
        struct RenderContext;
    }
} // namespace NS

namespace NS
{
    //! 真下の地形へ丸影を描く。地面がなければ描かず、高さに応じて薄く小さくする
    //! 地面の高さは歩ごとの Update で探して持ち、描く時は当たりを調べない
    class Shadow : public Object
    {
    public:
        //! 板とマテリアルの欄は、作る時に既定の名前の素材を AssetManager::Get() から引く
        Shadow();

        //! origin の真下 maxDrop 以内の地面の高さを collision で探して持つ。歩ごとに ActorList::SyncShadows が呼ぶ
        void Update(const IGetCollision* collision, const Vector3& origin) noexcept;
        //! 前の Update で真下に地面が見つかったか
        [[nodiscard]] bool HasGround() const noexcept { return m_hasGround; }
        //! 前の Update で探した地面のワールドの高さ (m)。地面が無ければ 0
        [[nodiscard]] float GroundHeight() const noexcept { return m_groundY; }
        //! 根の位置 origin の真下の、前の Update で探した地面の高さに影の板を置く行列。地面が無いか薄くて見えなければ無し
        [[nodiscard]] std::optional<Matrix> DrawWorldMatrix(const Vector3& origin) const noexcept;

        //! 根の真下の地面に影の板の DrawItem を積む。地面が無い・素材が描けない時は何も積まない
        //! @param[in] root 持ち主の根の世界行列。context.alpha で補間した物を渡す
        void Collect(const Gfx::RenderContext& context, const Matrix& root, std::vector<Gfx::DrawItem>& out) const;
        //! 段は Transparent、中心は根の位置、優先度は 0
        //! 箱は根の直下の落ちる範囲を全部覆う。影がどこに落ちても視錐台で外さない
        //! @param[in] root 持ち主の根の今の固定フレームの世界行列
        [[nodiscard]] Gfx::RenderBounds Bounds(const Matrix& root) const noexcept;

        //! 落下距離 dist に対する高さフェード係数 [0,1]。dist=0 で 1、dist>=maxDist で 0
        [[nodiscard]] static float ComputeFade(float dist, float maxDist) noexcept;

        // 接地シャドウの見た目を Inspector へ公開する。毎 Draw 読まれるのでライブで効く
        NS_REFLECT_BEGIN(Shadow, Object)
        NS_FIELD(m_mesh, "メッシュ")
        NS_FIELD(m_material, "マテリアル")
        NS_GLOBAL_FIELD(m_baseDiameter, "基本直径")
        NS_FIELD(m_maxDrop, "最大投影距離")
        NS_FIELD(m_surfaceOffset, "表面オフセット")
        NS_FIELD(m_baseAlpha, "基本不透明度")
        NS_REFLECT_END()

    private:
        // 素材は AssetManager が持つ。欄は nullptr にならない
        Mesh* m_mesh = nullptr;         // 影を描く板
        Material* m_material = nullptr; // 影のマテリアル

        float m_baseDiameter = 1.2f;   // 接地時の影の直径
        float m_maxDrop = 12.0f;       // 影が消える最大落下距離
        float m_surfaceOffset = 0.02f; // 地面へめり込ませない浮かせ量
        float m_baseAlpha = 0.5f;      // 接地時の不透明度
        //! 影の板 1 枚の置き場所と濃さ
        struct Plane
        {
            Matrix world = Matrix::Identity;
            float opacity = 0.0f;
        };
        //! 根の位置 origin の真下に置く板。濃さは origin と地面の高さの差で決める。地面が無いか薄くて見えなければ無し
        [[nodiscard]] std::optional<Plane> PlaneAt(const Vector3& origin) const noexcept;

        float m_groundY = 0.0f;        // 前の Update で探した地面のワールドの高さ。描く時はこの高さに置く。保存しない
        bool m_hasGround = false;      // 前の Update で地面が見つかったか。保存しない
    };
} // namespace NS

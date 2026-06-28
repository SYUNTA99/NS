#pragma once

/// @file ShadowComponent.h
/// @brief ShadowComponent — owner の真下の地面に半透明の接地シャドウを描く IRenderable Component
///
/// 下方向レイ vs 衝突 AABB で真下の地面を探し、命中面に水平な半透明クアッドを置く
/// 高さに応じて薄く・小さくし、空中での着地点予測キューにする。真下に地面が無ければ描かない
/// Alpha 合成で深度テストON 書込OFF の半透明バケットで描かれ、手前のジオメトリに遮蔽される
/// quad mesh / shadow material は scene が共有で持ち、SetResources で注入する。 本 Component は非所有

#include "Framework/Math/Math.h"
#include "Framework/Scene/Component.h"
#include "Framework/Scene/IRenderable.h"

#include <span>
#include <vector>

namespace NS::Graphics
{
    class StaticMesh;
    class Material;
} // namespace NS::Graphics

namespace NS::Scene
{
    /// owner 直下に接地シャドウを描く Component。mesh / material は呼出側の scene が寿命保証する
    class ShadowComponent : public Component, public IRenderable
    {
    public:
        /// 共有の quad mesh と shadow material を非所有で注入する。未設定なら Draw は何もしない
        void SetResources(NS::Graphics::StaticMesh* mesh, NS::Graphics::Material* material) noexcept;

        /// 地面探索に使う衝突 AABB を内部 vector にコピーして保持する。 呼出側 vector
        /// が再確保されても無効参照にならない
        void SetCollisionWorld(std::span<const NS::Math::AABB> world);

        /// 真下の地面に影クアッドを 1 描画呼出。地面が無い / リソース未設定なら何もしない
        void Draw(const RenderContext& context) override;
        /// 半透明バケットに分類させる
        [[nodiscard]] RenderBucket Bucket() const noexcept override { return RenderBucket::Transparent; }
        /// 半透明ソート用の中心で owner world 位置を返す
        [[nodiscard]] NS::Math::Vector3 SortCenter() const noexcept override;

        /// OwningScene に self を IRenderable 登録する
        void OnStart() override;
        /// OwningScene から self を解除する
        void OnEndPlay() override;

        /// origin から真下の -Y 方向へ maxDist まで衝突 AABB を探し、最近傍命中距離を outDist に返す
        /// 命中があれば true。GPU 非依存の純関数なのでテスト可能
        [[nodiscard]] static bool GroundBelow(const NS::Math::Vector3& origin,
                                              std::span<const NS::Math::AABB> world,
                                              float maxDist,
                                              float& outDist) noexcept;

        /// 落下距離 dist に対する高さフェード係数 [0,1]。dist=0 で 1、dist>=maxDist で 0
        [[nodiscard]] static float ComputeFade(float dist, float maxDist) noexcept;

        // 接地シャドウの見た目を Inspector へ公開する。 毎 Draw 読まれるのでライブで効く
        NS_REFLECT_BEGIN(ShadowComponent)
        NS_REFLECT_FIELD(m_baseDiameter, "Base Diameter")
        NS_REFLECT_FIELD(m_maxDrop, "Max Drop")
        NS_REFLECT_FIELD(m_surfaceOffset, "Surface Offset")
        NS_REFLECT_FIELD(m_baseAlpha, "Base Alpha")
        NS_REFLECT_END()

    private:
        NS::Graphics::StaticMesh* m_mesh = nullptr;
        NS::Graphics::Material* m_material = nullptr;
        std::vector<NS::Math::AABB> m_collisionWorld;

        float m_baseDiameter = 1.2f;
        float m_maxDrop = 12.0f;
        float m_surfaceOffset = 0.02f;
        float m_baseAlpha = 0.5f;
    };
} // namespace NS::Scene

#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Object
{
    class CharacterMovementComponent;
}

namespace NS::Game::Level
{
    class BreakableComponent;
    class MomentumComponent;

    //! @brief ぶつかった結果を自機側で決める Component
    //! @details 帯は Update より前。CharacterMovementComponent が動く前にその 1 固定ステップの結末を決めるので、
    //! 壁の手前で止められて速度を消された後から結果を推測し直さずに済む
    //! 相手は World::ForEachComponent で BreakableComponent を回って自分で探す
    //! NS::Physics は NS::Object を知らない決まりなので、掃引の戻り値から相手を引く経路は使えない
    //! 依存: NS::Object::CharacterMovementComponent, MomentumComponent, BreakableComponent
    class ImpactResolverComponent : public NS::Object::Component
    {
    public:
        ImpactResolverComponent() noexcept;

        //! 同じ配置物の移動と勢いを引き当てる。どちらか無ければ以後何もしない
        void OnStart() override;

        //! この固定ステップで重なる壊せる物を探し、向かっていれば反発を与える
        void OnUpdate() override;

        //! 直近の更新で反発した場合 true、それ以外の場合は false
        [[nodiscard]] bool DidRebound() const noexcept { return m_didRebound; }

        // 返り方は当てた時の手触りそのもの。プレイ中に Inspector で触って詰められるよう公開する
        NS_REFLECT_BEGIN(ImpactResolverComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_reboundSpeed, "反発初速")
        NS_REFLECT_FIELD(m_reboundUpSpeed, "反発の上向き初速")
        NS_REFLECT_END()

    private:
        // 重なっている壊せる物のうち中心が最も近い 1 体。無ければ nullptr
        // 事前条件: m_movement が非 null
        [[nodiscard]] BreakableComponent* FindOverlapped() const;

        float m_reboundSpeed = 9.0f;   // 反発の水平初速。通常速度 8.0 をわずかに超える
        float m_reboundUpSpeed = 3.0f; // 反発の上向き初速

        bool m_didRebound = false;                                    // 直近の更新で反発したか
        NS::Object::CharacterMovementComponent* m_movement = nullptr; // 同じ配置物の移動。非所有
        MomentumComponent* m_momentum = nullptr;                      // 同じ配置物の勢い。非所有
    };
} // namespace NS::Game::Level

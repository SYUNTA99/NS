#pragma once

#include "Runtime/Object/Component.h"

#if !defined(NS_SHIPPING)

namespace NS::Game::Level
{
    class MomentumComponent;

    //! @brief MomentumComponent の段を固定する検証用 Component
    //! @details MomentumComponent より先に走る。毎ステップ SetLevel し直し、固定中は昇格も降格も成立させない
    //! 出荷ビルドには入らない
    //! 依存: MomentumComponent
    class MomentumLockComponent : public NS::Object::Component
    {
    public:
        MomentumLockComponent() noexcept;

        //! 同じ配置物の MomentumComponent を引き当てる。見つからなければ以後何もしない
        void OnStart() override;

        //! 固定が有効なら段を 0〜1 へ丸めて置き直す
        void OnUpdate() override;

        NS_REFLECT_BEGIN(MomentumLockComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_lockLevel, "段の固定")
        NS_REFLECT_END()

    private:
        // -1=なし 0=通常 1=最高ダッシュ。時間昇格を待たずに衝突を検証するための固定で、
        // 触らない限り昇格の挙動を変えないよう既定は -1
        int m_lockLevel = -1;
        MomentumComponent* m_momentum = nullptr;
    };
} // namespace NS::Game::Level

#endif

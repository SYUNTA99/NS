#pragma once

#include "Game/Player/EffectLayerList.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"

namespace NS::Game::Player
{
    //! @brief 自機の溜めと放しのエフェクトの層を出し、出すと決めた記録を持つ
    //! @details 受け持つ層の名前は charge.・release.・slam. で始まる。今は層を 1 つも出さない
    //! 描画の無い世界でも記録は残し、試しと Replay は Layers を読む
    //! 優先度は Update 帯の +60。同じフレームの PlayerAppearance (+50) が回した玉の向きより後に走る
    //! 依存: EffectLayerList
    class ChargeEffects : public NS::Obj::Component
    {
    public:
        ChargeEffects() noexcept;

        //! 記録のフレームを 1 つ進める
        void OnUpdate() override;

        //! 出すと決めた層の記録
        [[nodiscard]] const EffectLayerList& Layers() const noexcept { return m_layers; }

        NS_REFLECT_NONE(ChargeEffects, NS::Obj::Component)

    private:
        EffectLayerList m_layers;
    };
} // namespace NS::Game::Player

#pragma once

namespace NS::Gfx
{
    class EffectScene;
}

namespace NS::Obj
{
    //! @brief エフェクトの窓口。シーンの EffectScene を引ける物が持つ
    //! @details Actor・UIActor・シーンが持つ。部品は持ち主の Actor から引く
    class IUseEffect
    {
    public:
        //! シーンの EffectScene。シーンに居ない間と、描画を持たないシーン (試し) では nullptr
        [[nodiscard]] virtual NS::Gfx::EffectScene* GetEffectScene() const noexcept = 0;

    protected:
        ~IUseEffect() = default;
    };
} // namespace NS::Obj

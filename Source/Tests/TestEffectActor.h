#pragma once

#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/CoreObject/SceneActor.h"

//! エフェクトの部品を持つだけのアクター。登録は iget_effect_test.cpp の NS_CLASS
class TestEffectActor : public NS::SceneActor
{
public:
    NS_REFLECT_NONE(TestEffectActor, NS::SceneActor)

protected:
    void OnInit() override { CreateClassObj(m_effect, this, &TransformObj()); }
};

//! 部品を持たず、受けた置き場だけを答える IGetEffect。EffectObj を単体で試す時に渡す
class TestEffectUser final : public NS::IGetEffect
{
public:
    //! @param[in] effects 答える置き場。nullptr は描画の無い世界
    explicit TestEffectUser(NS::Gfx::EffectScene* effects = nullptr) noexcept : m_effects(effects) {}

    [[nodiscard]] NS::EffectObj* GetEffect() const noexcept override { return nullptr; }
    [[nodiscard]] NS::Gfx::EffectScene* GetEffectScene() const noexcept override { return m_effects; }

private:
    NS::Gfx::EffectScene* m_effects = nullptr;
};

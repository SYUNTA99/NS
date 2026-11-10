#pragma once

#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/SceneActor.h"

//! エフェクトの部品を持つだけのアクター。登録は iget_effect_test.cpp の NS_CLASS
class TestEffectActor : public NS::SceneActor
{
public:
    NS_REFLECT_NONE(TestEffectActor, NS::SceneActor)

protected:
    void OnInit() override { CreateClassObj(m_effect); }
};

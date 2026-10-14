#pragma once

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/EffectKeeper.h"

class TestEffectActor : public NS::Obj::Actor
{
protected:
    void OnInit() override { (void)CreateSubObj(m_effectKeeper); }
};

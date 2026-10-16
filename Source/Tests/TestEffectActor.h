#pragma once

#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/EffectKeeper.h"

class TestEffectActor : public NS::SceneActor
{
protected:
    void OnInit() override { CreateSubObj(m_effectKeeper, this); }
};

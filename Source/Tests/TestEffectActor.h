#pragma once

#include "NSlib/Object/SceneActor.h"
#include "NSlib/Object/SubObjects/EffectKeeper.h"

class TestEffectActor : public NS::Obj::SceneActor
{
protected:
    void OnInit() override { (void)CreateSubObj(m_effectKeeper); }
};

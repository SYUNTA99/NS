#pragma once

#include "NSlib/CoreObject/ClassObjs/Effect.h"
#include "NSlib/CoreObject/SceneActor.h"

class TestEffectActor : public NS::SceneActor
{
protected:
    void OnInit() override { CreateClassObj(m_effect); }
};

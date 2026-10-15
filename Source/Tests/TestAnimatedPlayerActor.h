#pragma once

#include "Game/PlayerActor.h"
#include "NSlib/Object/SubObjects/Animation.h"

class TestAnimatedPlayerActor final : public PlayerActor
{
protected:
    void OnInit() override
    {
        PlayerActor::OnInit();
        CreateSubObj(m_animation);
    }
};

#pragma once

#include "Game/Player.h"
#include "NSlib/Object/SubObjects/Animation.h"

class TestAnimatedPlayer final : public Player
{
protected:
    void OnInit() override
    {
        Player::OnInit();
        (void)CreateSubObj(m_animation);
    }
};

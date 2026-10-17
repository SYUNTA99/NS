#pragma once

#include "Game/Player/PlayerClips.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/Animation.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <cstddef>

class TestAnimatedPlayerActor final : public PlayerActor
{
protected:
    void OnInit() override
    {
        PlayerActor::OnInit();
        CreateClassObj(m_animation, this);
    }
};

// 自機の役の欄に字面を書いて読み直す。返すのは書けなかった欄の数
inline std::size_t UseTestClips(PlayerActor& player, const nlohmann::json& fields)
{
    const std::size_t failed = NS::ApplyJsonFields(player.Clips(), fields);
    player.ReloadAssets();
    return failed;
}

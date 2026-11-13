#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "Tests/TestSceneSetup.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace
{
    // 表示名と、今の float32 の秒の計算で切れていたフレーム数 (dt = 1/60)
    const std::vector<std::pair<std::string, int>> k_FrameFields = {
        {"コヨーテのフレーム数", 1},
        {"先行入力のフレーム数", 15},
        {"ダッシュと反動の後に空中の操作が戻るフレーム数", 30},
        {"よじ登りのフレーム数", 15},
        {"反動の操作なしのフレーム数", 9},
        {"反動の補正が終わるフレーム数", 36},
        {"エアダッシュのフレーム数", 36},
    };

    void ExpectFrameCounts(const nlohmann::json& fields)
    {
        for (const std::pair<std::string, int>& field : k_FrameFields)
        {
            ASSERT_TRUE(fields.contains(field.first)) << field.first;
            EXPECT_TRUE(fields.at(field.first).is_number_integer()) << field.first;
            EXPECT_EQ(fields.at(field.first), field.second) << field.first;
        }
    }
} // namespace

TEST(PlayerFrameParams, DefaultsAreFrameCounts)
{
    const GL::Player::PlayerParams params{};
    ExpectFrameCounts(NS::SerializeClassObjFields(&params));
}

TEST(PlayerFrameParams, ActorFileHoldsSameFrameCounts)
{
    NS::Scene scene;
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorJson(doc, "PlayerActor", 1);
    scene.ResetFromJson(doc);
    const PlayerActor* player = static_cast<PlayerActor*>(scene.Actors().FindByActorId(1));
    ASSERT_NE(player, nullptr);
    ExpectFrameCounts(NS::SerializeClassObjFields(&player->Params()));
}

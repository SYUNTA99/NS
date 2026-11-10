#include "Game/Level/CameraHitModifiers.h"
#include "Game/Level/HitReactionItems.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "Tests/TestSceneSetup.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

// Game が足した当たりの反応の項目は、NSlib の項目と同じ登録の表から引け、持ち主の Stop で外れる

namespace
{
    PlayerActor* PlacePlayer(NS::Scene& scene)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "PlayerActor", 1, NS::Vector3{0.0f, 1.0f, 0.0f});
        scene.ResetFromJson(doc);
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }

    // 名前から作った項目が、その名前の型か
    template <class Info> bool MakesItemOf(std::string_view typeName)
    {
        const NS::HitReactionItemKind* kind = NS::HitReactionItemRegistry::Get().Find(typeName);
        if (kind == nullptr)
        {
            return false;
        }
        const NS::HitReactionItem item = kind->make();
        return item.As<Info>() != nullptr;
    }
} // namespace

// テストの実行でも Game の項目の自己登録が効き、ファイルの種類の名前から型を引ける
TEST(HitReactionItems, GameItemsAreFoundInTheRegistryByTheirTypeNames)
{
    EXPECT_TRUE(MakesItemOf<GL::Level::HitCameraSinkInfo>("CameraSink"));
    EXPECT_TRUE(MakesItemOf<GL::Level::HitCameraSinkReleaseInfo>("CameraSinkRelease"));
    EXPECT_TRUE(MakesItemOf<GL::Level::HitZoomRollInfo>("ZoomRoll"));
    EXPECT_TRUE(MakesItemOf<GL::Level::HitCameraLurchInfo>("CameraLurch"));
    EXPECT_TRUE(MakesItemOf<GL::Level::HitCameraAwaySwayInfo>("CameraAwaySway"));
    EXPECT_TRUE(MakesItemOf<GL::Level::HitOthersStopInfo>("OthersStop"));
}

// 持ち主の Stop は、持ち主が頼んだカメラの効果のうちトラウマのほかを外し、ほかの物が頼んだ効果は残す
TEST(HitReactionItems, StopRemovesTheOwnersCameraEffectsButKeepsTheTrauma)
{
    NS::Scene scene;
    PlayerActor* player = PlacePlayer(scene);
    ASSERT_NE(player, nullptr);
    NS::HitReaction* reaction = player->HitReactionClassObj();
    ASSERT_NE(reaction, nullptr);
    NS::CameraManager* cameras = scene.GetCameraManager();
    const NS::HitReactionStart start{
        .reaction = reaction, .owner = NS::HitReactionSelf{.actor = player}, .length = 4};
    GL::Level::HitCameraSinkInfo{}.Start(start);
    NS::HitCameraTraumaInfo{}.Start(start);
    ASSERT_TRUE(GL::Level::StartCameraZoomRoll(&scene, GL::Level::CameraZoomRollDesc{.zoom = 1.2f, .holdFrames = 5}));
    ASSERT_NE(cameras->FindModifier<GL::Level::CameraSinkModifier>(), nullptr);
    const float trauma = cameras->Trauma();
    ASSERT_GT(trauma, 0.0f);

    reaction->Stop(NS::HitReactionSelf{.actor = player, .model = player->ModelClassObj()});

    EXPECT_EQ(cameras->FindModifier<GL::Level::CameraSinkModifier>(), nullptr);
    EXPECT_FLOAT_EQ(cameras->Trauma(), trauma);
    EXPECT_FLOAT_EQ(GL::Level::CurrentCameraZoomRoll(*cameras).zoom, 1.2f);
}

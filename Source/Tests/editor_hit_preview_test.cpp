#include "Editor/HitPreview.h"
#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactResolver.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/FileSystem.h"
#include "Tests/TestHitTimelines.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

// エディタの下見が、編集中の場面の写しの中で選んだ面の位置へ当て、当たりの前後を記録する事を縛る

namespace
{
    using namespace NS::Game::Level;

    constexpr std::uint32_t k_RockId = 2;

    // 床の当たりは組み込みの立方体のメッシュから作るので、資産の置き場を差す。描き手は要らない
    struct PreviewAssets
    {
        NS::Obj::AssetManager assets{NS::Platform::FileSystem::ContentRoot()};
        NS::Editor::HitPreviewWorld World() { return NS::Editor::HitPreviewWorld{.assets = &assets}; }
    };

    // 床 (id 100)・自機 (id 1)・その前の置物 (id 2) の場面の文書
    // 置物は半径 1.5 m の球で、床に乗った自機の玉が面の真ん中より下まで届く大きさにする
    nlohmann::json MakePreviewSceneJson()
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json floor = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(floor, "MapParts");
        NS::Obj::SetObjectJsonId(floor, 100);
        NS::Obj::SetObjectPosition(floor, NS::Core::Vector3{0.0f, -0.5f, 0.0f});
        NS::Obj::SetObjectScale(floor, NS::Core::Vector3{60.0f, 1.0f, 60.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(floor));
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, k_RockId);
        NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{0.0f, 1.5f, 7.0f});
        NS::Obj::SetObjectScale(rock, NS::Core::Vector3{3.0f, 3.0f, 3.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        return doc;
    }

    // 縮みの形 (検知から 4 フレーム)・止め 2 フレーム・反動と相手を飛ばす。行の番号は 0 から 3
    HitTimeline MakePreviewTimeline()
    {
        ShapeEvent shrink;
        shrink.along.count = 1;
        shrink.along.keys[0] = NS::Obj::Curve::Key{0.0f, 0.8f};
        shrink.height.count = 1;
        shrink.height.keys[0] = NS::Obj::Curve::Key{0.0f, 1.0f};
        HitTimeline timeline;
        timeline.events = {{shrink, 0, 4, HitDirection::Any},
                           {HitStopEvent{}, 1, 2, HitDirection::Any},
                           {ReboundEvent{}, 3, 1, HitDirection::Any},
                           {TargetLaunchEvent{}, 3, 1, HitDirection::Any}};
        return timeline;
    }

    NS::Editor::HitPreviewDesc MakeDesc(float faceU, float faceV)
    {
        NS::Editor::HitPreviewDesc desc;
        desc.targetId = k_RockId;
        desc.faceU = faceU;
        desc.faceV = faceV;
        desc.charge01 = 1.0f;
        return desc;
    }
} // namespace

// R-6: 選んだ面の位置へ当たり、段は面の判定のとおりに決まる。検知は狙ったフレーム数の後
TEST(EditorHitPreview, HitsThePickedPointOnTheFace)
{
    const ScopedHitTimelineDirectory directory("EditorHitPreview");
    ScopedHitTimelineDirectory::SetBothTiers(MakePreviewTimeline());
    const nlohmann::json snapshot = MakePreviewSceneJson();
    PreviewAssets assets;

    const NS::Editor::HitPreviewResult center =
        NS::Editor::RunHitPreview(snapshot, MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(center.hit) << center.error;
    EXPECT_EQ(center.impact.targetId, k_RockId);
    EXPECT_EQ(center.impact.tier, HitTier::Center);
    EXPECT_NEAR(center.impact.faceU, 0.0f, 0.05f);
    EXPECT_NEAR(center.impact.faceV, 0.0f, 0.05f);
    EXPECT_NEAR(center.detectionIndex, center.desc.leadFrames, 1);

    const NS::Editor::HitPreviewResult edge =
        NS::Editor::RunHitPreview(snapshot, MakeDesc(0.85f, 0.0f), assets.World());
    ASSERT_TRUE(edge.hit) << edge.error;
    EXPECT_EQ(edge.impact.tier, HitTier::Wide);
    EXPECT_NEAR(edge.impact.faceU, 0.85f, 0.05f);
}

// R-8 の下地: 帯へ重ねるため、事象が実際に始まったフレームを行の番号で記録する
TEST(EditorHitPreview, RecordsTheFrameEachRowStarted)
{
    const ScopedHitTimelineDirectory directory("EditorHitPreviewRows");
    ScopedHitTimelineDirectory::SetBothTiers(MakePreviewTimeline());
    PreviewAssets assets;
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    ASSERT_GE(result.detectionIndex, 0);

    // 行ごとに、始まったフレームを検知のフレームから数える
    std::vector<int> startedAt(4, -100);
    for (std::size_t i = 0; i < result.frames.size(); ++i)
    {
        for (const std::size_t row : result.frames[i].startedRows)
        {
            ASSERT_LT(row, startedAt.size());
            startedAt[row] = static_cast<int>(i) - result.detectionIndex;
        }
    }
    EXPECT_EQ(startedAt, (std::vector<int>{0, 1, 3, 3}));
    // 反動が終わるまで記録が続く
    const bool rebounded = std::any_of(result.frames.begin(),
                                       result.frames.end(),
                                       [](const NS::Editor::HitPreviewFrame& frame) { return frame.rebounding; });
    EXPECT_TRUE(rebounded);
    EXPECT_FALSE(result.frames.back().rebounding);
}

// R-10・R-7 の下地: 下見は写しの中で閉じ、同じ条件なら毎回同じ記録になる。選んだフレームの場面も記録と同じ所に居る
TEST(EditorHitPreview, LeavesTheEditedSceneAloneAndRepeats)
{
    const ScopedHitTimelineDirectory directory("EditorHitPreviewRepeat");
    ScopedHitTimelineDirectory::SetBothTiers(MakePreviewTimeline());
    NS::Obj::Scene edited;
    edited.LoadJson(MakePreviewSceneJson());
    edited.SetSimulationEnabled(false);
    const nlohmann::json before = edited.ToJson();

    PreviewAssets assets;
    const NS::Editor::HitPreviewResult first = NS::Editor::RunHitPreview(before, MakeDesc(0.3f, 0.2f), assets.World());
    const NS::Editor::HitPreviewResult second = NS::Editor::RunHitPreview(before, MakeDesc(0.3f, 0.2f), assets.World());
    ASSERT_TRUE(first.hit) << first.error;
    EXPECT_EQ(edited.ToJson(), before);
    EXPECT_EQ(second.detectionIndex, first.detectionIndex);
    ASSERT_EQ(second.frames.size(), first.frames.size());
    for (std::size_t i = 0; i < first.frames.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_TRUE(second.frames[i].playerPosition == first.frames[i].playerPosition);
        EXPECT_EQ(second.frames[i].startedRows, first.frames[i].startedRows);
    }

    const int picked = first.detectionIndex + 2;
    std::unique_ptr<NS::Obj::Scene> scene = NS::Editor::BuildHitPreviewSceneAt(before, first, picked, assets.World());
    ASSERT_NE(scene, nullptr);
    const NS::Obj::Actor* player = scene->Objects().FindByObjectId(1);
    ASSERT_NE(player, nullptr);
    EXPECT_TRUE(player->Root().Position() == first.frames[static_cast<std::size_t>(picked)].playerPosition);
}

// 選んだ相手が場面に居ない時は、当てずにエラーを返す
TEST(EditorHitPreview, MissingTargetIsAnError)
{
    NS::Editor::HitPreviewDesc desc = MakeDesc(0.0f, 0.0f);
    desc.targetId = 999;
    const NS::Editor::HitPreviewResult result = NS::Editor::RunHitPreview(MakePreviewSceneJson(), desc);
    EXPECT_FALSE(result.hit);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.frames.empty());
}

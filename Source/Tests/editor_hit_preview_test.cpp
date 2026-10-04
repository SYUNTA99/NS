#include "Editor/EditorObjects.h"
#include "Editor/HitPreview.h"
#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/FileSystem.h"
#include "Runtime/Platform/Input.h"
#include "Tests/TestHitTimelines.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
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

// hitstop-and-shake R-4-2: 帯に重ねるため、揺れ・トラウマ・世界の速さをフレームごとに記録する
TEST(EditorHitPreview, RecordsTheShakeAndTheWorldSpeedEachFrame)
{
    HitTimeline timeline = MakePreviewTimeline();
    CameraTraumaEvent trauma;
    trauma.trauma = 0.8f;
    GradualReleaseEvent release;
    release.startSpeed = 0.25f;
    timeline.events.push_back({trauma, 1, 1, HitDirection::Any});
    timeline.events.push_back({release, 3, 1, HitDirection::Any});
    const ScopedHitTimelineDirectory directory("EditorHitPreviewShake");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    PreviewAssets assets;
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.6f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    ASSERT_GE(result.detectionIndex, 0);
    const std::size_t traumaFrame = static_cast<std::size_t>(result.detectionIndex + 1);
    const std::size_t releaseFrame = static_cast<std::size_t>(result.detectionIndex + 3);
    ASSERT_GT(result.frames.size(), releaseFrame);
    EXPECT_FLOAT_EQ(result.frames[traumaFrame - 1].trauma, 0.0f);
    EXPECT_GT(result.frames[traumaFrame].trauma, 0.0f);
    EXPECT_GT(result.frames[traumaFrame].shakeAngles.Length(), 0.0f);
    EXPECT_FLOAT_EQ(result.frames[releaseFrame - 1].worldSpeed, 1.0f);
    EXPECT_LT(result.frames[releaseFrame].worldSpeed, 1.0f);
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

// R-10: 下見の振動は手元のパッドへ送らず、フレームごとの記録に残す
TEST(EditorHitPreview, HoldsPadVibrationInTheRecord)
{
    const ScopedHitTimelineDirectory directory("EditorHitPreviewPad");
    HitTimeline timeline = MakePreviewTimeline();
    PadVibrationEvent pad;
    pad.left.count = 1;
    pad.left.keys[0] = NS::Obj::Curve::Key{0.0f, 0.7f};
    timeline.events.push_back({pad, 0, 4, HitDirection::Any});
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    NS::Platform::Input& input = NS::Platform::Input::Get();
    ASSERT_TRUE(input.Gamepad().SetVibration(0.1f, 0.0f));
    PreviewAssets assets;

    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    EXPECT_FLOAT_EQ(result.frames[static_cast<std::size_t>(result.detectionIndex)].pad.left, 0.7f);
    EXPECT_FLOAT_EQ(input.Gamepad().Vibration().left, 0.1f);
    EXPECT_FALSE(input.IsNeutral());
    input.Gamepad().StopVibration();
}

// 記録の 1 行は Replay の hits.jsonl と同じ鍵を同じ並びで持ち、f は検知のフレーム
TEST(EditorHitPreview, HitLineHasTheReplayKeys)
{
    const ScopedHitTimelineDirectory directory("EditorHitPreviewLine");
    ScopedHitTimelineDirectory::SetBothTiers(MakePreviewTimeline());
    PreviewAssets assets;
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;

    const nlohmann::ordered_json line = nlohmann::ordered_json::parse(NS::Editor::HitPreviewHitLine(result));
    std::vector<std::string> keys;
    for (nlohmann::ordered_json::const_iterator it = line.begin(); it != line.end(); ++it)
    {
        keys.push_back(it.key());
    }
    const std::vector<std::string> expected = {
        "f",     "victim",    "power",       "charge",     "centerCoef", "offset",     "tier",          "centerHit",
        "broke", "hitstop",   "launchSpeed", "launchVel",  "launchDist", "launchApex", "selfKnockback", "selfApex",
        "dir",   "targetPos", "cameraShake", "flashStart", "zoomStart",  "rollStart",  "padStart"};
    EXPECT_EQ(keys, expected);
    EXPECT_EQ(line["f"].get<int>(), result.detectionIndex);
    EXPECT_EQ(line["victim"].get<std::uint32_t>(), k_RockId);
}

// R-7: 下見は試しの道 (入力を止めて突進を直接頼み、場面の 1 歩で進める) と同じ記録を出す
TEST(EditorHitPreview, MatchesADirectRunFromTheSamePlacement)
{
    const ScopedHitTimelineDirectory directory("EditorHitPreviewDirect");
    ScopedHitTimelineDirectory::SetBothTiers(MakePreviewTimeline());
    PreviewAssets assets;
    const nlohmann::json snapshot = MakePreviewSceneJson();
    const NS::Editor::HitPreviewResult preview =
        NS::Editor::RunHitPreview(snapshot, MakeDesc(-0.4f, 0.1f), assets.World());
    ASSERT_TRUE(preview.hit) << preview.error;

    nlohmann::json placed = snapshot;
    const std::size_t playerIndex = NS::Editor::FindPlayerObjectIndex(placed);
    ASSERT_NE(playerIndex, NS::Obj::k_NoObjectIndex);
    NS::Obj::SetObjectPosition(NS::Obj::SceneJsonObjects(placed)[playerIndex], preview.playerStart);
    NS::Obj::Scene scene;
    scene.SetAssets(&assets.assets);
    scene.LoadJson(placed);
    Player* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    player->Input().SetLocked(true);
    player->RequestBodySlam(preview.desc.charge01, preview.direction);
    int detection = -1;
    for (int step = 0; step < static_cast<int>(preview.frames.size()); ++step)
    {
        scene.OnUpdate();
        SCOPED_TRACE(step);
        EXPECT_TRUE(player->Root().Position() == preview.frames[static_cast<std::size_t>(step)].playerPosition);
        if (detection < 0 && player->Resolver().LastImpact().sequence != 0)
        {
            detection = step;
        }
    }
    EXPECT_EQ(detection, preview.detectionIndex);
    const ImpactRecord& direct = player->Resolver().LastImpact();
    EXPECT_EQ(direct.tier, preview.impact.tier);
    EXPECT_FLOAT_EQ(direct.power, preview.impact.power);
    EXPECT_FLOAT_EQ(direct.faceU, preview.impact.faceU);
    EXPECT_FLOAT_EQ(direct.faceV, preview.impact.faceV);
    EXPECT_TRUE(direct.selfVelocity == preview.impact.selfVelocity);
    EXPECT_TRUE(direct.launchVelocity == preview.impact.launchVelocity);
}

// 相手を選んでいない時は、自機に一番近い、突進が当たる体と面を持つ配置物を相手にする
TEST(EditorHitPreview, PicksTheNearestTargetWhenNoneIsChosen)
{
    const ScopedHitTimelineDirectory directory("EditorHitPreviewNearest");
    ScopedHitTimelineDirectory::SetBothTiers(MakePreviewTimeline());
    nlohmann::json snapshot = MakePreviewSceneJson();
    nlohmann::json distant = NS::Obj::MakeObjectJson();
    NS::Obj::SetObjectJsonClass(distant, "MapObj");
    NS::Obj::SetObjectJsonId(distant, 3);
    NS::Obj::SetObjectPosition(distant, NS::Core::Vector3{0.0f, 0.5f, 25.0f});
    NS::Obj::SceneJsonObjects(snapshot).push_back(std::move(distant));
    PreviewAssets assets;
    NS::Editor::HitPreviewDesc desc = MakeDesc(0.0f, 0.0f);
    desc.targetId = 0;

    const NS::Editor::HitPreviewResult result = NS::Editor::RunHitPreview(snapshot, desc, assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    EXPECT_EQ(result.desc.targetId, k_RockId);
    EXPECT_EQ(result.impact.targetId, k_RockId);
}

// 沈む揺れも下見の記録に載る。帯の下の折れ線が読む
TEST(EditorHitPreview, RecordsTheSinkShakeEachFrame)
{
    HitTimeline timeline = MakePreviewTimeline();
    timeline.events.push_back({CameraSinkEvent{}, 1, 20, HitDirection::Any});
    const ScopedHitTimelineDirectory directory("EditorHitPreviewSink");
    ScopedHitTimelineDirectory::SetBothTiers(timeline);
    PreviewAssets assets;
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    ASSERT_GE(result.detectionIndex, 0);
    const std::size_t sinkFrame = static_cast<std::size_t>(result.detectionIndex + 1);
    ASSERT_GT(result.frames.size(), sinkFrame + 1);
    EXPECT_FLOAT_EQ(result.frames[sinkFrame - 1].sinkPixels, 0.0f);
    EXPECT_LT(result.frames[sinkFrame].sinkPixels, 0.0f);
    EXPECT_LT(result.frames[sinkFrame + 1].sinkPixels, result.frames[sinkFrame].sinkPixels);
}

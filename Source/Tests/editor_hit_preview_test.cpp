#include "Editor/ActionTimelineEdit.h"
#include "Editor/EditorObjects.h"
#include "Editor/HitPreview.h"
#include "Editor/TimelinePreview.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/ActionTimeline.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Windows/FileSystem.h"
#include "NSlib/Windows/Input.h"
#include "Tests/TestActorFile.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// エディタの下見が、編集中の場面の写しの中で選んだ面の位置へ当て、当たりの前後を記録する事を縛る

namespace
{
    using namespace GL::Level;

    constexpr NS::uint32 k_RockId = 2;

    // 床の当たりは組み込みの立方体のメッシュから作るので、資産の置き場を差す。描き手は要らない
    struct PreviewAssets
    {
        NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
        NS::AssetManagerScope useAssets{assets};
        NS::Editor::HitPreviewWorld World() { return NS::Editor::HitPreviewWorld{}; }
    };

    // 床 (id 100)・自機 (id 1)・その前の置物 (id 2) の場面の文書
    // 置物は半径 1.5 m の球で、床に乗った自機の玉が面の真ん中より下まで届く大きさにする
    nlohmann::json MakePreviewSceneJson()
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json floor = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(floor, "MapPartsActor");
        NS::SetObjectJsonId(floor, 100);
        NS::Editor::SetObjectPosition(floor, NS::Vector3{0.0f, -0.5f, 0.0f});
        NS::Editor::SetObjectScale(floor, NS::Vector3{60.0f, 1.0f, 60.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(floor));
        nlohmann::json player = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(player, "PlayerActor");
        NS::SetObjectJsonId(player, 1);
        NS::Editor::SetObjectPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::MakeObjectJson();
        NS::SetObjectJsonActorFile(rock, "PropActor");
        NS::SetObjectJsonId(rock, k_RockId);
        NS::Editor::SetObjectPosition(rock, NS::Vector3{0.0f, 1.5f, 7.0f});
        NS::Editor::SetObjectScale(rock, NS::Vector3{3.0f, 3.0f, 3.0f});
        NS::SceneJsonObjects(doc).push_back(std::move(rock));
        return doc;
    }

    // 下見の自機の資料。当たりの束 (止め 2 フレーム) に足す項目と、明けの束に足す項目
    struct PreviewData
    {
        std::vector<NS::HitReactionEntry> hitItems;
        std::vector<NS::HitReactionEntry> releaseItems;
    };

    // 当たる直前の動作の白 4 つ。始まりは 0・2・4・4 で、動作の最後のフレーム 6 が検知のフレーム
    NS::ActorAction MakePreviewAction()
    {
        NS::ActorAction action;
        action.name = std::string{BeforeContactActionName(HitTier::Center)};
        action.events = {{NS::HitFlashItem{}, 0, 1},
                         {NS::HitFlashItem{}, 2, 1},
                         {NS::HitFlashItem{}, 4, 1},
                         {NS::HitFlashItem{}, 4, 3}};
        return action;
    }

    // 試しの間だけ、自機の .actor の当たりの束を止め 2 フレームと data の項目に、当たる直前の動作を白 4 つに替える
    // 明けの束は出荷の物の後ろに data の項目を足す。明けは検知の 3 フレーム後
    class ScopedPreviewActorFile
    {
    public:
        explicit ScopedPreviewActorFile(const PreviewData& data = {})
            : m_edit("PlayerActor", [&data](nlohmann::json& actorFile) {
                  std::vector<NS::HitReactionSet> sets = NS::ReadHitReactionSets("PlayerActor", actorFile);
                  for (const HitTier tier : {HitTier::Center, HitTier::Wide})
                  {
                      const std::string hit = TackleSetName("体当たり", tier);
                      const std::string release = TackleSetName("明け", tier);
                      for (NS::HitReactionSet& set : sets)
                      {
                          if (set.name == hit)
                          {
                              set.stopFrames = 2;
                              set.entries = data.hitItems;
                          }
                          if (set.name == release)
                          {
                              set.entries.insert(set.entries.end(), data.releaseItems.begin(), data.releaseItems.end());
                          }
                      }
                  }
                  NS::WriteHitReactionSets(actorFile, sets);
                  NS::WriteActorActions(actorFile, {MakePreviewAction()});
              })
        {
        }

    private:
        ScopedActorFileEdit m_edit;
    };

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

// 選んだ面の位置へ当たり、段は面の判定のとおりに決まる。検知は狙ったフレーム数の後
TEST(EditorHitPreview, HitsThePickedPointOnTheFace)
{
    const ScopedPreviewActorFile actorFile;
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

// 帯に重ねるため、揺れ・トラウマ・世界の速さをフレームごとに記録する
TEST(EditorHitPreview, RecordsTheShakeAndTheWorldSpeedEachFrame)
{
    NS::HitCameraTraumaItem trauma;
    trauma.trauma = 0.8f;
    NS::HitWorldSpeedItem slow;
    slow.startSpeed = 0.25f;
    const ScopedPreviewActorFile actorFile(PreviewData{.hitItems = {{trauma, 1}}, .releaseItems = {{slow, 1}}});
    PreviewAssets assets;
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.6f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    ASSERT_GE(result.detectionIndex, 0);
    // 当たりの束は検知の次のフレーム、明けの束は止め 2 フレームの後に始まる
    const std::size_t traumaFrame = static_cast<std::size_t>(result.detectionIndex + 1);
    const std::size_t releaseFrame = static_cast<std::size_t>(result.detectionIndex + 3);
    ASSERT_GT(result.frames.size(), releaseFrame);
    EXPECT_FLOAT_EQ(result.frames[traumaFrame - 1].trauma, 0.0f);
    EXPECT_GT(result.frames[traumaFrame].trauma, 0.0f);
    EXPECT_GT(result.frames[traumaFrame].shakeAngles.Length(), 0.0f);
    EXPECT_FLOAT_EQ(result.frames[releaseFrame - 1].worldSpeed, 1.0f);
    EXPECT_LT(result.frames[releaseFrame].worldSpeed, 1.0f);
}

// 帯へ重ねるため、動作の事象が実際に始まったフレームを、動作に入った歩を 0 にして行の番号で記録する
TEST(EditorHitPreview, RecordsTheFrameEachRowStarted)
{
    const ScopedPreviewActorFile actorFile;
    PreviewAssets assets;
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    ASSERT_GE(result.detectionIndex, 0);

    const std::string name{BeforeContactActionName(HitTier::Center)};
    EXPECT_EQ(NS::Editor::RowStartFrames(result, name, 0), (std::vector<int>{0}));
    EXPECT_EQ(NS::Editor::RowStartFrames(result, name, 1), (std::vector<int>{2}));
    EXPECT_EQ(NS::Editor::RowStartFrames(result, name, 2), (std::vector<int>{4}));
    EXPECT_EQ(NS::Editor::RowStartFrames(result, name, 3), (std::vector<int>{4}));
    // 動作の事象は、当たる直前の状態を出る検知のフレームより後に始まらない
    for (std::size_t i = static_cast<std::size_t>(result.detectionIndex) + 1; i < result.frames.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_TRUE(result.frames[i].startedEvents.empty());
    }
    // 反動が終わるまで記録が続く
    const bool rebounded = std::any_of(result.frames.begin(),
                                       result.frames.end(),
                                       [](const NS::Editor::HitPreviewFrame& frame) { return frame.rebounding; });
    EXPECT_TRUE(rebounded);
    EXPECT_FALSE(result.frames.back().rebounding);
}

// 下見は写しの中で閉じ、同じ条件なら毎回同じ記録になる
TEST(EditorHitPreview, LeavesTheEditedSceneAloneAndRepeats)
{
    const ScopedPreviewActorFile actorFile;
    NS::Scene edited;
    edited.ResetFromJson(MakePreviewSceneJson());
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
        EXPECT_EQ(second.frames[i].action, first.frames[i].action);
        EXPECT_EQ(second.frames[i].actionFrame, first.frames[i].actionFrame);
        EXPECT_EQ(second.frames[i].startedEvents, first.frames[i].startedEvents);
    }
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

// 下見の振動は手元のパッドへ送らず、フレームごとの記録に残す
TEST(EditorHitPreview, HoldsPadVibrationInTheRecord)
{
    NS::HitPadVibrationItem pad;
    pad.left.count = 1;
    pad.left.keys[0] = NS::Curve::Key{0.0f, 0.7f};
    const ScopedPreviewActorFile actorFile(PreviewData{.hitItems = {{pad, 4}}});
    NS::OS::Input& input = NS::OS::Input::Get();
    ASSERT_TRUE(input.Gamepad().SetVibration(0.1f, 0.0f));
    PreviewAssets assets;

    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    // 当たりの束は検知の次のフレームに始まる
    EXPECT_FLOAT_EQ(result.frames[static_cast<std::size_t>(result.detectionIndex + 1)].pad.left, 0.7f);
    EXPECT_FLOAT_EQ(input.Gamepad().Vibration().left, 0.1f);
    EXPECT_FALSE(input.IsNeutral());
    input.Gamepad().StopVibration();
}

// 記録の 1 行は当たりの記録 hits.jsonl と同じ鍵を同じ並びで持ち、f は検知のフレーム
TEST(EditorHitPreview, HitLineHasTheRecordKeys)
{
    const ScopedPreviewActorFile actorFile;
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
    EXPECT_EQ(line["victim"].get<NS::uint32>(), k_RockId);
    // 返りの始めの値は束の項目が持ち、当たりの記録に写さない
    for (const char* key : {"cameraShake", "flashStart", "zoomStart", "rollStart", "padStart"})
    {
        EXPECT_TRUE(line[key].is_null()) << key;
    }
}

// 下見は試しの道 (入力を止めて突進を直接頼み、場面の 1 歩で進める) と同じ記録を出す
TEST(EditorHitPreview, MatchesADirectRunFromTheSamePlacement)
{
    const ScopedPreviewActorFile actorFile;
    PreviewAssets assets;
    const nlohmann::json snapshot = MakePreviewSceneJson();
    const NS::Editor::HitPreviewResult preview =
        NS::Editor::RunHitPreview(snapshot, MakeDesc(-0.4f, 0.1f), assets.World());
    ASSERT_TRUE(preview.hit) << preview.error;

    nlohmann::json placed = snapshot;
    const std::size_t playerIndex = NS::Editor::FindPlayerObjectIndex(placed);
    ASSERT_NE(playerIndex, NS::k_NoObjectIndex);
    NS::Editor::SetObjectPosition(NS::SceneJsonObjects(placed)[playerIndex], preview.playerStart);
    NS::Scene scene;
    scene.ResetFromJson(placed);
    PlayerActor* player = FindPlayer(scene.Objects());
    ASSERT_NE(player, nullptr);
    player->Input().SetLocked(true);
    player->RequestBodySlam(preview.desc.charge01, preview.direction);
    int detection = -1;
    for (int step = 0; step < static_cast<int>(preview.frames.size()); ++step)
    {
        scene.OnUpdate();
        SCOPED_TRACE(step);
        EXPECT_TRUE(player->Root().Position() == preview.frames[static_cast<std::size_t>(step)].playerPosition);
        if (detection < 0 && player->Resolver().Answer().lastImpact.sequence != 0)
        {
            detection = step;
        }
    }
    EXPECT_EQ(detection, preview.detectionIndex);
    const ImpactRecord& direct = player->Resolver().Answer().lastImpact;
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
    const ScopedPreviewActorFile actorFile;
    nlohmann::json snapshot = MakePreviewSceneJson();
    nlohmann::json distant = NS::MakeObjectJson();
    NS::SetObjectJsonActorFile(distant, "PropActor");
    NS::SetObjectJsonId(distant, 3);
    NS::Editor::SetObjectPosition(distant, NS::Vector3{0.0f, 0.5f, 25.0f});
    NS::SceneJsonObjects(snapshot).push_back(std::move(distant));
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
    const ScopedPreviewActorFile actorFile(PreviewData{.hitItems = {{NS::HitCameraSinkItem{}, 20}}});
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

TEST(EditorHitPreview, PlaybackEvaluatesTheSceneAtTheRecordedFrameAndCanRewind)
{
    const ScopedPreviewActorFile actorFile;
    PreviewAssets assets;
    const nlohmann::json snapshot = MakePreviewSceneJson();
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(snapshot, MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    NS::Editor::TimelinePreview playback;
    playback.Reset(
        [&snapshot, &result, &assets] { return NS::Editor::MakeHitPreviewScene(snapshot, result, assets.World()); },
        {-result.detectionIndex, static_cast<int>(result.frames.size()) - 1 - result.detectionIndex});
    const int indexes[] = {
        0, result.detectionIndex, static_cast<int>(result.frames.size()) - 1, 0, result.detectionIndex};
    for (const int index : indexes)
    {
        SCOPED_TRACE(index);
        NS::Scene* scene = playback.Seek(index - result.detectionIndex);
        ASSERT_NE(scene, nullptr);
        PlayerActor* player = FindPlayer(scene->Objects());
        ASSERT_NE(player, nullptr);
        const NS::Editor::HitPreviewFrame& frame = result.frames[static_cast<std::size_t>(index)];
        EXPECT_TRUE(player->Root().Position() == frame.playerPosition);
        EXPECT_TRUE(player->ModelClassObj()->GetDrawScale() == frame.shape);
        EXPECT_EQ(player->IsHitStopping(), frame.hitStopping);
        EXPECT_EQ(player->IsRebounding(), frame.rebounding);
        EXPECT_FLOAT_EQ(scene->WorldSpeed(), frame.worldSpeed);
        EXPECT_TRUE(scene->IsSimulationPaused());
    }
    EXPECT_EQ(snapshot, MakePreviewSceneJson());
}

// 下見の場面はゲームと同じ始まりを通る。プレイ開始の写しを持ち、置いた物は開始の姿を控えている
TEST(EditorHitPreview, StartsTheSceneLikeTheGame)
{
    const ScopedPreviewActorFile actorFile;
    PreviewAssets assets;
    const nlohmann::json snapshot = MakePreviewSceneJson();
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(snapshot, MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    const std::unique_ptr<NS::Scene> scene = NS::Editor::MakeHitPreviewScene(snapshot, result, assets.World());
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(NS::SceneJsonObjects(scene->PlayBaseline()).size(), NS::SceneJsonObjects(snapshot).size());

    // 動かしてからやり直しを送ると、開始の時の姿へ戻る。開始の姿を控えていない物は戻らない
    PlayerActor* player = FindPlayer(scene->Objects());
    ASSERT_NE(player, nullptr);
    const NS::Vector3 playerStart = player->Root().Position();
    player->Root().SetPosition(playerStart + NS::Vector3{4.0f, 3.0f, -5.0f});
    EXPECT_TRUE(SendMsgCourseRestart(*player));
    EXPECT_TRUE(player->Root().Position() == playerStart);

    NS::SceneActor* rock = scene->Objects().FindByObjectId(k_RockId);
    ASSERT_NE(rock, nullptr);
    EXPECT_TRUE(SendMsgCourseRestart(*rock));
}

// 反動が先に終わっても、当たりの束の一番長い項目が終わるまで記録する
TEST(EditorHitPreview, RecordsUntilTheLastEventEvenWhenTheReboundHasAlreadyEnded)
{
    const ScopedPreviewActorFile actorFile(PreviewData{.hitItems = {{NS::HitCameraSinkItem{}, 160}}});
    PreviewAssets assets;
    const NS::Editor::HitPreviewResult result =
        NS::Editor::RunHitPreview(MakePreviewSceneJson(), MakeDesc(0.0f, 0.0f), assets.World());
    ASSERT_TRUE(result.hit) << result.error;
    EXPECT_GE(static_cast<int>(result.frames.size()) - result.detectionIndex, 160);
}

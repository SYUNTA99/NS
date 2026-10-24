#include "Editor/EditorActors.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Windows/FileSystem.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <vector>

// 同じ当たりと同じ溜めで、カメラの揺れのずれと角度をフレームごとに書き出す
// 揺れを作り直す前と後で同じ場面を書き出し、隣り合うフレームのずれの差の最大を並べる
// 書き出し先は build/ShakeTrace/trace.json。演出は自機と置物の .actor の束を読む

namespace
{
    using GL::Level::HitTier;

    constexpr int k_HitFrames = 30;    // 検知のフレームから書き出すフレーム数
    constexpr int k_ChargeFrames = 60; // 溜めの揺れを書き出すフレーム数

    // 自機 (id 1) と、その前の置物 (id 2)。rockX を横へずらすと外れになる
    PlayerActor* PlaceShakeScene(NS::Scene& scene, float rockX)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json player = NS::MakeActorJson();
        NS::SetActorJsonFile(player, "PlayerActor");
        NS::SetActorJsonId(player, 1);
        NS::Editor::SetActorJsonPosition(player, NS::Vector3{0.0f, 1.0f, 0.0f});
        NS::SceneJsonActors(doc).push_back(std::move(player));
        nlohmann::json rock = NS::MakeActorJson();
        NS::SetActorJsonFile(rock, "PropActor");
        NS::SetActorJsonId(rock, 2);
        NS::Editor::SetActorJsonPosition(rock, NS::Vector3{rockX, 0.5f, 0.6f});
        NS::SceneJsonActors(doc).push_back(std::move(rock));
        scene.ResetFromJson(doc);
        NS::OBB floor{};
        floor.center = NS::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }

    // 当たりの揺れ。平行移動の揺れのずれ (x が右、y が上、m)・トラウマの揺れの角度 (横・縦・傾き、度)・
    // 沈む揺れの縦のずれ (y、高さ 1080 の画面の画素、下が負)
    struct HitShakeTrace
    {
        HitTier tier = HitTier::Center;
        std::vector<NS::Vector3> offsets;
        std::vector<NS::Vector3> angles;
        std::vector<NS::Vector3> sinks;
    };

    // 突進を頼んで場面を回し、検知のフレームから k_HitFrames フレームぶんの揺れのずれを並べる。当たらなければ空
    HitShakeTrace RecordHitShake(float rockX)
    {
        HitShakeTrace trace;
        NS::Scene scene;
        PlayerActor* player = PlaceShakeScene(scene, rockX);
        if (player == nullptr)
        {
            return trace;
        }
        player->RequestBodySlam(1.0f, NS::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 60 + k_HitFrames && static_cast<int>(trace.offsets.size()) < k_HitFrames; ++frame)
        {
            scene.OnUpdate();
            if (trace.offsets.empty() && player->Resolver().Answer().lastImpact.sequence == 0)
            {
                continue;
            }
            trace.tier = player->Resolver().Answer().lastImpact.tier;
            NS::Vector3 offset{};
            NS::Vector3 angles{};
            NS::Vector3 sink{};
            const NS::CameraManager* cameras = player->GetCameraManager();
            if (cameras != nullptr)
            {
                const NS::CameraShakeModifier* shake = cameras->FindModifier<NS::CameraShakeModifier>();
                if (shake != nullptr)
                {
                    offset = NS::Vector3{shake->Offset().x, shake->Offset().y, 0.0f};
                }
                const NS::CameraTraumaModifier* trauma = cameras->FindModifier<NS::CameraTraumaModifier>();
                if (trauma != nullptr)
                {
                    angles = trauma->Angles();
                }
                const NS::CameraSinkModifier* sinking = cameras->FindModifier<NS::CameraSinkModifier>();
                if (sinking != nullptr)
                {
                    sink = NS::Vector3{0.0f, sinking->Pixels(), 0.0f};
                }
            }
            trace.offsets.push_back(offset);
            trace.angles.push_back(angles);
            trace.sinks.push_back(sink);
        }
        return trace;
    }

    // 溜めの量が 1 秒で 1 まで上がり、そのまま押し続ける相手
    class ChargingTargetActor final : public NS::SceneActor
    {
    public:
        // 接地と溜めを仮想カメラの口へ渡す
        void Feed(NS::ThirdPersonFollow* vcam) const
        {
            NS_ASSERT(Game, vcam != nullptr, "Feed: 渡したアクターか部品が無い");
            vcam->SetFollowMotion(true, NS::Vector3{});
            NS::FollowChargeDesc charge{};
            charge.held = true;
            charge.charge01 = std::min(static_cast<float>(frame) / 60.0f, 1.0f);
            (void)vcam->SetFollowCharge(charge);
        }
        int frame = 0;
    };

    // 溜めの揺れの角度 (横・縦・傾き、度) を k_ChargeFrames フレームぶん並べる
    std::vector<NS::Vector3> RecordChargeShake()
    {
        std::vector<NS::Vector3> offsets;
        NS::Scene scene;
        ChargingTargetActor* target = scene.SpawnActor<ChargingTargetActor>("target");
        GL::Level::FollowCameraActor* actor = scene.SpawnActor<GL::Level::FollowCameraActor>("FollowCameraActor");
        if (target == nullptr || actor == nullptr)
        {
            return offsets;
        }
        NS::ThirdPersonFollow& vcam = actor->Vcam();
        NS::ApplyJsonFields(&vcam, nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}}});
        // 欄を書いた後に、エディタと同じく相手を取得し直す
        actor->ResolveActorRefs();
        for (int frame = 0; frame < k_ChargeFrames; ++frame)
        {
            target->frame = frame;
            target->Feed(&vcam);
            scene.OnUpdate();
            NS::Vector3 angles{};
            const NS::CameraManager* cameras = scene.GetCameraManager();
            if (cameras != nullptr)
            {
                const NS::CameraTraumaModifier* trauma = cameras->FindModifier<NS::CameraTraumaModifier>();
                if (trauma != nullptr)
                {
                    angles = trauma->Angles();
                }
            }
            offsets.push_back(angles);
        }
        return offsets;
    }

    // 隣り合うフレームのずれの差の最大 ÷ ずれの大きさの最大。揺れが無ければ 0
    // 揺れが始まるフレーム (前のフレームのずれが 0) の差は数えない。始まりは一撃で、跳ばないのは揺れの途中の話
    float MaxStepRatio(const std::vector<NS::Vector3>& offsets)
    {
        float largest = 0.0f;
        float step = 0.0f;
        for (std::size_t i = 0; i < offsets.size(); ++i)
        {
            largest = std::max(largest, offsets[i].Length());
            if (i > 0 && offsets[i - 1].LengthSquared() > 0.0f)
            {
                step = std::max(step, (offsets[i] - offsets[i - 1]).Length());
            }
        }
        if (largest <= 0.0f)
        {
            return 0.0f;
        }
        return step / largest;
    }

    // 当たりの揺れの差の比。平行移動の揺れ・沈む揺れ・トラウマの揺れの角度の順に、出ている物の比
    float HitStepRatio(const HitShakeTrace& trace)
    {
        const float offsetRatio = MaxStepRatio(trace.offsets);
        if (offsetRatio > 0.0f)
        {
            return offsetRatio;
        }
        const float sinkRatio = MaxStepRatio(trace.sinks);
        if (sinkRatio > 0.0f)
        {
            return sinkRatio;
        }
        return MaxStepRatio(trace.angles);
    }

    nlohmann::ordered_json SeriesJson(const std::vector<NS::Vector3>& series)
    {
        nlohmann::ordered_json list = nlohmann::ordered_json::array();
        for (const NS::Vector3& value : series)
        {
            list.push_back(nlohmann::ordered_json{value.x, value.y, value.z});
        }
        return list;
    }
} // namespace

TEST(ShakeTrace, WritesTheShakeOfTheSameHitsAndCharge)
{
    const HitShakeTrace center = RecordHitShake(0.0f);
    const HitShakeTrace miss = RecordHitShake(0.75f);
    const std::vector<NS::Vector3> charge = RecordChargeShake();
    ASSERT_EQ(center.offsets.size(), static_cast<std::size_t>(k_HitFrames));
    ASSERT_EQ(miss.offsets.size(), static_cast<std::size_t>(k_HitFrames));
    ASSERT_EQ(charge.size(), static_cast<std::size_t>(k_ChargeFrames));
    EXPECT_EQ(center.tier, HitTier::Center);
    EXPECT_EQ(miss.tier, HitTier::Wide);

    const float centerRatio = HitStepRatio(center);
    const float missRatio = HitStepRatio(miss);
    const float chargeRatio = MaxStepRatio(charge);
    // 揺れが書き出されていなければ基準にならない
    EXPECT_GT(centerRatio, 0.0f);
    EXPECT_GT(missRatio, 0.0f);
    EXPECT_GT(chargeRatio, 0.0f);
    // 外れと溜めはトラウマの揺れ。方形の波 (直す前 外れ 1.71・溜め 1.98) のように振れ幅の倍近くを
    // 1 フレームで跳ばない
    EXPECT_LT(missRatio, 0.6f);
    EXPECT_LT(chargeRatio, 0.6f);

    nlohmann::ordered_json root;
    root["frameSeconds"] = 1.0f / 60.0f;
    root["centerMaxStepRatio"] = centerRatio;
    root["missMaxStepRatio"] = missRatio;
    root["chargeMaxStepRatio"] = chargeRatio;
    // 平行移動のずれは m、角度は度
    root["center"] = SeriesJson(center.offsets);
    root["centerAngles"] = SeriesJson(center.angles);
    // 沈む揺れの縦のずれは高さ 1080 の画面の画素
    root["centerSink"] = SeriesJson(center.sinks);
    root["miss"] = SeriesJson(miss.offsets);
    root["missAngles"] = SeriesJson(miss.angles);
    root["chargeAngles"] = SeriesJson(charge);
    const std::string text = root.dump(1);
    using NS::OS::FileSystem;
    const std::string directory =
        FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "ShakeTrace");
    (void)FileSystem::CreateDirectories(directory);
    const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
    EXPECT_TRUE(FileSystem::WriteAllBytes(FileSystem::Combine(directory, "trace.json"),
                                          std::span<const std::byte>(raw, text.size())));
}

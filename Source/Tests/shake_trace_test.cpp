#include "Game/Level/FollowCamera.h"
#include "Game/Level/HitTimeline.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Object/CameraTarget.h"
#include "Runtime/Object/Components/CameraManager.h"
#include "Runtime/Object/Components/CameraModifier.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/FileSystem.h"
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

// 同じ当たりと同じ溜めで、カメラの揺れのずれと角度をフレームごとに書き出す (R-4-3)
// 揺れを作り直す前と後で同じ場面を書き出し、隣り合うフレームのずれの差の最大を並べる
// 書き出し先は build/ShakeTrace/trace.json。タイムラインは置き場の既定 (Assets/HitTimelines) を読む

namespace
{
    using NS::Game::Level::HitTier;

    constexpr int k_HitFrames = 30;    // 検知のフレームから書き出すフレーム数
    constexpr int k_ChargeFrames = 60; // 溜めの揺れを書き出すフレーム数

    // 自機 (id 1) と、その前の置物 (id 2)。rockX を横へずらすと外れになる
    Player* PlaceShakeScene(NS::Obj::Scene& scene, float rockX)
    {
        nlohmann::json doc = NS::Obj::MakeSceneJson();
        nlohmann::json player = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(player, "Player");
        NS::Obj::SetObjectJsonId(player, 1);
        NS::Obj::SetObjectPosition(player, NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(player));
        nlohmann::json rock = NS::Obj::MakeObjectJson();
        NS::Obj::SetObjectJsonClass(rock, "MapObj");
        NS::Obj::SetObjectJsonId(rock, 2);
        NS::Obj::SetObjectPosition(rock, NS::Core::Vector3{rockX, 0.5f, 0.6f});
        NS::Obj::SceneJsonObjects(doc).push_back(std::move(rock));
        scene.LoadJson(doc);
        NS::Core::OBB floor{};
        floor.center = NS::Core::Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, NS::Core::Vector3{}, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Obj::Cast<Player>(scene.Objects().FindByObjectId(1));
    }

    // 当たりの揺れ。平行移動の揺れのずれ (x が右、y が上、m) とトラウマの揺れの角度 (横・縦・傾き、度)
    struct HitShakeTrace
    {
        HitTier tier = HitTier::Center;
        std::vector<NS::Core::Vector3> offsets;
        std::vector<NS::Core::Vector3> angles;
    };

    // 突進を頼んで場面を回し、検知のフレームから k_HitFrames フレームぶんの揺れのずれを並べる。当たらなければ空
    HitShakeTrace RecordHitShake(float rockX)
    {
        HitShakeTrace trace;
        NS::Obj::Scene scene;
        Player* player = PlaceShakeScene(scene, rockX);
        if (player == nullptr)
        {
            return trace;
        }
        player->RequestBodySlam(1.0f, NS::Core::Vector3{0.0f, 0.0f, 1.0f});
        for (int frame = 0; frame < 60 + k_HitFrames && static_cast<int>(trace.offsets.size()) < k_HitFrames; ++frame)
        {
            scene.OnUpdate();
            if (trace.offsets.empty() && player->Resolver().LastImpact().sequence == 0)
            {
                continue;
            }
            trace.tier = player->Resolver().LastImpact().tier;
            NS::Core::Vector3 offset{};
            NS::Core::Vector3 angles{};
            const NS::Obj::CameraManager* cameras = player->GetCameraManager();
            if (cameras != nullptr)
            {
                const NS::Obj::CameraShakeModifier* shake = cameras->FindModifier<NS::Obj::CameraShakeModifier>();
                if (shake != nullptr)
                {
                    offset = NS::Core::Vector3{shake->Offset().x, shake->Offset().y, 0.0f};
                }
                const NS::Obj::CameraTraumaModifier* trauma = cameras->FindModifier<NS::Obj::CameraTraumaModifier>();
                if (trauma != nullptr)
                {
                    angles = trauma->Angles();
                }
            }
            trace.offsets.push_back(offset);
            trace.angles.push_back(angles);
        }
        return trace;
    }

    // 溜めの量が 1 秒で 1 まで上がり、そのまま押し続ける相手
    class ChargingTarget final : public NS::Obj::Actor, public NS::Obj::ICameraTarget
    {
    public:
        const NS::Obj::ICameraTarget* GetCameraTarget() const noexcept override { return this; }
        NS::Obj::CameraTargetState GetCameraTargetState() const noexcept override
        {
            NS::Obj::CameraTargetState state;
            state.grounded = true;
            state.hasCharge = true;
            state.charge.held = true;
            state.charge.charge01 = std::min(static_cast<float>(frame) / 60.0f, 1.0f);
            return state;
        }
        int frame = 0;
    };

    // 溜めの揺れの角度 (横・縦・傾き、度) を k_ChargeFrames フレームぶん並べる
    std::vector<NS::Core::Vector3> RecordChargeShake()
    {
        std::vector<NS::Core::Vector3> offsets;
        NS::Obj::Scene scene;
        ChargingTarget* target =
            NS::Obj::Cast<ChargingTarget>(scene.SpawnObject(std::make_unique<ChargingTarget>(), "target"));
        NS::Game::Level::FollowCamera* actor = scene.SpawnTransient<NS::Game::Level::FollowCamera>();
        if (target == nullptr || actor == nullptr)
        {
            return offsets;
        }
        NS::Obj::ThirdPersonFollow& vcam = actor->Vcam();
        NS::Obj::ApplyJsonFields(vcam, nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}}});
        for (int frame = 0; frame < k_ChargeFrames; ++frame)
        {
            target->frame = frame;
            scene.OnUpdate();
            NS::Core::Vector3 angles{};
            const NS::Obj::CameraManager* cameras = scene.GetCameraManager();
            if (cameras != nullptr)
            {
                const NS::Obj::CameraTraumaModifier* trauma = cameras->FindModifier<NS::Obj::CameraTraumaModifier>();
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
    float MaxStepRatio(const std::vector<NS::Core::Vector3>& offsets)
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

    // 当たりの揺れの差の比。平行移動の揺れが出ていればその比、出ていなければトラウマの揺れの角度の比
    float HitStepRatio(const HitShakeTrace& trace)
    {
        const float offsetRatio = MaxStepRatio(trace.offsets);
        if (offsetRatio > 0.0f)
        {
            return offsetRatio;
        }
        return MaxStepRatio(trace.angles);
    }

    nlohmann::ordered_json SeriesJson(const std::vector<NS::Core::Vector3>& series)
    {
        nlohmann::ordered_json list = nlohmann::ordered_json::array();
        for (const NS::Core::Vector3& value : series)
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
    const std::vector<NS::Core::Vector3> charge = RecordChargeShake();
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
    // R-3-1: 外れと溜めはトラウマの揺れ。方形の波 (直す前 外れ 1.71・溜め 1.98) のように振れ幅の倍近くを 1
    // フレームで跳ばない
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
    root["miss"] = SeriesJson(miss.offsets);
    root["missAngles"] = SeriesJson(miss.angles);
    root["chargeAngles"] = SeriesJson(charge);
    const std::string text = root.dump(1);
    using NS::Platform::FileSystem;
    const std::string directory =
        FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "ShakeTrace");
    (void)FileSystem::CreateDirectories(directory);
    const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
    EXPECT_TRUE(FileSystem::WriteAllBytes(FileSystem::Combine(directory, "trace.json"),
                                          std::span<const std::byte>(raw, text.size())));
}

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
#include <memory>
#include <span>
#include <string>
#include <vector>

// 同じ当たりと同じ溜めで、カメラの揺れのずれをフレームごとに書き出す (R-4-3)
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

    // 当たりの揺れのずれ。x がカメラの右、y が上 (m)
    struct HitShakeTrace
    {
        HitTier tier = HitTier::Center;
        std::vector<NS::Core::Vector2> offsets;
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
            NS::Core::Vector2 offset{0.0f, 0.0f};
            const NS::Obj::CameraManager* cameras = player->GetCameraManager();
            if (cameras != nullptr)
            {
                const NS::Obj::CameraShakeModifier* shake = cameras->FindModifier<NS::Obj::CameraShakeModifier>();
                if (shake != nullptr)
                {
                    offset = shake->Offset();
                }
            }
            trace.offsets.push_back(offset);
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

    // 溜めの揺れのずれ (m、カメラの上の向き) を k_ChargeFrames フレームぶん並べる
    std::vector<float> RecordChargeShake()
    {
        std::vector<float> offsets;
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
            actor->Update();
            offsets.push_back(vcam.ChargeShake());
        }
        return offsets;
    }

    // 隣り合うフレームのずれの差の最大 ÷ ずれの大きさの最大。揺れが無ければ 0
    float MaxStepRatio(const std::vector<NS::Core::Vector2>& offsets)
    {
        float largest = 0.0f;
        float step = 0.0f;
        for (std::size_t i = 0; i < offsets.size(); ++i)
        {
            largest = std::max(largest, std::hypot(offsets[i].x, offsets[i].y));
            if (i > 0)
            {
                step = std::max(step, std::hypot(offsets[i].x - offsets[i - 1].x, offsets[i].y - offsets[i - 1].y));
            }
        }
        if (largest <= 0.0f)
        {
            return 0.0f;
        }
        return step / largest;
    }

    nlohmann::ordered_json OffsetsJson(const std::vector<NS::Core::Vector2>& offsets)
    {
        nlohmann::ordered_json list = nlohmann::ordered_json::array();
        for (const NS::Core::Vector2& offset : offsets)
        {
            list.push_back(nlohmann::ordered_json{offset.x, offset.y});
        }
        return list;
    }
} // namespace

TEST(ShakeTrace, WritesTheShakeOfTheSameHitsAndCharge)
{
    const HitShakeTrace center = RecordHitShake(0.0f);
    const HitShakeTrace miss = RecordHitShake(0.75f);
    const std::vector<float> charge = RecordChargeShake();
    ASSERT_EQ(center.offsets.size(), static_cast<std::size_t>(k_HitFrames));
    ASSERT_EQ(miss.offsets.size(), static_cast<std::size_t>(k_HitFrames));
    ASSERT_EQ(charge.size(), static_cast<std::size_t>(k_ChargeFrames));
    EXPECT_EQ(center.tier, HitTier::Center);
    EXPECT_EQ(miss.tier, HitTier::Wide);

    std::vector<NS::Core::Vector2> chargeOffsets;
    for (const float up : charge)
    {
        chargeOffsets.push_back(NS::Core::Vector2{0.0f, up});
    }
    const float centerRatio = MaxStepRatio(center.offsets);
    const float missRatio = MaxStepRatio(miss.offsets);
    const float chargeRatio = MaxStepRatio(chargeOffsets);
    // 揺れが書き出されていなければ基準にならない
    EXPECT_GT(centerRatio, 0.0f);
    EXPECT_GT(missRatio, 0.0f);
    EXPECT_GT(chargeRatio, 0.0f);

    nlohmann::ordered_json root;
    root["frameSeconds"] = 1.0f / 60.0f;
    root["centerMaxStepRatio"] = centerRatio;
    root["missMaxStepRatio"] = missRatio;
    root["chargeMaxStepRatio"] = chargeRatio;
    root["center"] = OffsetsJson(center.offsets);
    root["miss"] = OffsetsJson(miss.offsets);
    root["charge"] = OffsetsJson(chargeOffsets);
    const std::string text = root.dump(1);
    using NS::Platform::FileSystem;
    const std::string directory =
        FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "ShakeTrace");
    (void)FileSystem::CreateDirectories(directory);
    const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
    EXPECT_TRUE(FileSystem::WriteAllBytes(FileSystem::Combine(directory, "trace.json"),
                                          std::span<const std::byte>(raw, text.size())));
}

#include "Editor/EditorActors.h"
#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "Tests/TestSceneSetup.h"
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

// 同じ当たりで、カメラの揺れの角度と沈みをフレームごとに書き出す
// 揺れを作り直す前と後で同じ場面を書き出し、隣り合うフレームのずれの差の最大を並べる
// 書き出し先は build/ShakeTrace/trace.json。演出は自機と置物の .actor の束を読む

namespace
{
    using GL::Level::HitTier;

    constexpr int k_HitFrames = 30; // 検知のフレームから書き出すフレーム数

    // 自機 (id 1) と、その前の置物 (id 2)。rockX を横へずらすと外れになる
    PlayerActor* PlaceShakeScene(NS::Scene& scene, float rockX)
    {
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "PlayerActor", 1, NS::Vector3{0.0f, 1.0f, 0.0f});
        AddActorJson(doc, "PropActor", 2, NS::Vector3{rockX, 0.5f, 0.6f});
        scene.ResetFromJson(doc);
        AddTerrainBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, 100.0f, 0.5f, 100.0f);
        PlaceViewCamera(scene, NS::Vector3{}, NS::Vector3{0.0f, 0.0f, 1.0f});
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }

    // 当たりの揺れ。トラウマの揺れの角度 (横・縦・傾き、度)・
    // 沈む揺れの縦のずれ (y、高さ 1080 の画面の画素、下が負)
    struct HitShakeTrace
    {
        HitTier tier = HitTier::Center;
        std::vector<NS::Vector3> angles;
        std::vector<NS::Vector3> sinks;
    };

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
        for (int frame = 0; frame < 60 + k_HitFrames && static_cast<int>(trace.angles.size()) < k_HitFrames; ++frame)
        {
            scene.OnUpdate();
            if (trace.angles.empty() && player->Resolver().Answer().lastImpact.sequence == 0)
            {
                continue;
            }
            trace.tier = player->Resolver().Answer().lastImpact.tier;
            NS::Vector3 angles{};
            NS::Vector3 sink{};
            const NS::CameraManager* cameras = player->GetCameraManager();
            if (cameras != nullptr)
            {
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
            trace.angles.push_back(angles);
            trace.sinks.push_back(sink);
        }
        return trace;
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

    float HitStepRatio(const HitShakeTrace& trace)
    {
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

TEST(ShakeTrace, WritesTheShakeOfTheSameHits)
{
    const HitShakeTrace center = RecordHitShake(0.0f);
    const HitShakeTrace miss = RecordHitShake(0.75f);
    ASSERT_EQ(center.angles.size(), static_cast<std::size_t>(k_HitFrames));
    ASSERT_EQ(miss.angles.size(), static_cast<std::size_t>(k_HitFrames));
    EXPECT_EQ(center.tier, HitTier::Center);
    EXPECT_EQ(miss.tier, HitTier::Wide);

    const float centerRatio = HitStepRatio(center);
    const float missRatio = HitStepRatio(miss);
    // 揺れが書き出されていなければ基準にならない
    EXPECT_GT(centerRatio, 0.0f);
    EXPECT_GT(missRatio, 0.0f);
    // 外れはトラウマの揺れ。方形の波 (直す前 1.71) のように振れ幅の倍近くを 1 フレームで跳ばない
    EXPECT_LT(missRatio, 0.6f);

    nlohmann::ordered_json root;
    root["frameSeconds"] = 1.0f / 60.0f;
    root["centerMaxStepRatio"] = centerRatio;
    root["missMaxStepRatio"] = missRatio;
    // 平行移動のずれは m、角度は度
    root["centerAngles"] = SeriesJson(center.angles);
    // 沈む揺れの縦のずれは高さ 1080 の画面の画素
    root["centerSink"] = SeriesJson(center.sinks);
    root["missAngles"] = SeriesJson(miss.angles);
    const std::string text = root.dump(1);
    using NS::OS::FileSystem;
    const std::string directory =
        FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "ShakeTrace");
    (void)FileSystem::CreateDirectories(directory);
    const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
    EXPECT_TRUE(FileSystem::WriteAllBytes(FileSystem::Combine(directory, "trace.json"),
                                          std::span<const std::byte>(raw, text.size())));
}

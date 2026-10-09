#include "Game/Level/FollowCamera.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/CameraTarget.h"
#include "NSlib/Object/SubObjects/ThirdPersonFollow.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <memory>

// 体当たりから止めの明けまでは、カメラの距離と溜めの締めを放した時のまま保つ。当たる瞬間に画面が引くと、
// 揺れを見せたい止めの間に二人が小さく遠ざかる。明けたら締めを戻し、距離も普通の追い方へ戻す

namespace
{
    // 溜め・突進・明けの状態を試しが書く追われる物。動かない
    class FramingTargetProbe final : public NS::Obj::Actor, public NS::Obj::ICameraTarget
    {
    public:
        const NS::Obj::ICameraTarget* GetCameraTarget() const noexcept override { return this; }
        NS::Obj::CameraTargetState GetCameraTargetState() const noexcept override
        {
            NS::Obj::CameraTargetState state;
            state.grounded = grounded;
            state.hasCharge = true;
            state.charge.held = held;
            state.charge.charge01 = charge01;
            state.framingHeld = framingHeld;
            state.charge.hasAimTarget = hasAim;
            state.charge.aimTargetCenter = aim;
            state.charge.aimTargetRadius = 0.5f;
            return state;
        }

        bool hasAim = false;
        float charge01 = 1.0f;
        NS::Vector3 aim{};
        bool grounded = true;
        bool held = true;
        bool framingHeld = false;
    };

    struct FramingScene
    {
        NS::Obj::Scene scene;
        FramingTargetProbe* target = nullptr;
        GL::Level::FollowCamera* camera = nullptr;

        FramingScene()
        {
            target =
                static_cast<FramingTargetProbe*>(scene.SpawnObject(std::make_unique<FramingTargetProbe>(), "target"));
            camera = scene.SpawnTransient<GL::Level::FollowCamera>();
            NS::Obj::ApplyJsonFields(camera->Vcam(),
                                     nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}}});
        }

        void Run(int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                camera->Update();
            }
        }
    };
} // namespace

// 溜めきりで放し、浮いた突進と止めの間は保つ。明けたら締めは戻しのフレーム数で 0 になり、距離は空中の距離へ動く
TEST(FollowImpactFraming, HeldFramingKeepsDistanceAndNarrowUntilTheRelease)
{
    FramingScene world;
    world.Run(120);
    const NS::Obj::ThirdPersonFollow& vcam = world.camera->Vcam();
    const float narrow = vcam.ChargeNarrowDegrees();
    const float distance = vcam.Distance();
    ASSERT_GT(narrow, 0.0f);

    world.target->held = false;
    world.target->grounded = false;
    world.target->framingHeld = true;
    for (int frame = 0; frame < 20; ++frame)
    {
        SCOPED_TRACE(frame);
        world.Run(1);
        EXPECT_FLOAT_EQ(vcam.ChargeNarrowDegrees(), narrow);
        EXPECT_FLOAT_EQ(vcam.Distance(), distance);
    }

    world.target->framingHeld = false;
    world.Run(1);
    EXPECT_LT(vcam.ChargeNarrowDegrees(), narrow);
    world.Run(10);
    EXPECT_FLOAT_EQ(vcam.ChargeNarrowDegrees(), 0.0f);
    EXPECT_GT(vcam.Distance(), distance);
}

// 狙う相手のいる溜めきりでは、自機と相手の真ん中を画面の縦の中心へ寄せる。注視点は頭の高さにあるので、寄せないと
// 二人は画面の下の方に小さく写る。寄せは注視点だけを下げて下を向かせ、カメラの位置は下げない。下げると見下ろす
// 角が浅くなり、奥の相手が自機の真後ろに重なる
TEST(FollowImpactFraming, ChargeWithATargetCentersThePairVertically)
{
    FramingScene alone;
    alone.Run(120);
    const NS::Obj::CameraPose alonePose = alone.camera->Vcam().EvaluatePose(1.0f);

    FramingScene world;
    world.target->hasAim = true;
    world.target->aim = NS::Vector3{0.0f, 0.2f, 3.0f};
    world.Run(120);
    const NS::Obj::CameraPose pose = world.camera->Vcam().EvaluatePose(1.0f);
    EXPECT_NEAR(pose.position.y, alonePose.position.y, 1.0e-4f);
    EXPECT_LT(pose.target.y, alonePose.target.y);
    NS::Vector3 forward = pose.target - pose.position;
    forward.Normalize();
    NS::Vector3 right = NS::Cross(NS::Vector3{0.0f, 1.0f, 0.0f}, forward);
    right.Normalize();
    const NS::Vector3 up = NS::Cross(forward, right);
    const NS::Vector3 middle = (world.target->Root().Position() + world.target->aim) * 0.5f;
    EXPECT_NEAR(NS::Dot(middle - pose.position, up), 0.0f, 0.02f);
}

// 寄せる量は溜めの量の 2 乗 (欄「溜めの寄りの効き方の指数」) で増える。溜めるにつれて速まりながら下を向き、
// 相手を見付けたフレームに一気に動かない
TEST(FollowImpactFraming, CenteringGrowsWithTheCharge)
{
    FramingScene alone;
    alone.Run(120);
    const float aloneLook = alone.camera->Vcam().EvaluatePose(1.0f).target.y;
    float drop[2] = {0.0f, 0.0f};
    const float charges[2] = {0.25f, 1.0f};
    for (int i = 0; i < 2; ++i)
    {
        FramingScene world;
        world.target->hasAim = true;
        world.target->aim = NS::Vector3{0.0f, 0.2f, 3.0f};
        world.target->charge01 = charges[i];
        world.Run(120);
        drop[i] = aloneLook - world.camera->Vcam().EvaluatePose(1.0f).target.y;
    }
    ASSERT_GT(drop[1], 0.0f);
    EXPECT_NEAR(drop[0], drop[1] * 0.25f * 0.25f, drop[1] * 0.02f);
}

// 視野の締めは 溜めで締める視野角 × 溜めの量の指数乗。溜めの半分では 4 分の 1 で、終わりにかけて速まる。
// 指数を 1 にすると溜めの量に比例する
TEST(FollowImpactFraming, NarrowSpeedsUpTowardTheFullCharge)
{
    FramingScene world;
    world.target->charge01 = 0.5f;
    world.Run(2);
    EXPECT_FLOAT_EQ(world.camera->Vcam().ChargeNarrowDegrees(), 15.0f * 0.25f);
    NS::Obj::ApplyJsonFields(world.camera->Vcam(), nlohmann::json{{"溜めの寄りの効き方の指数", 1.0f}});
    world.Run(1);
    EXPECT_FLOAT_EQ(world.camera->Vcam().ChargeNarrowDegrees(), 15.0f * 0.5f);
    world.target->charge01 = 1.0f;
    NS::Obj::ApplyJsonFields(world.camera->Vcam(), nlohmann::json{{"溜めの寄りの効き方の指数", 2.0f}});
    world.Run(1);
    EXPECT_FLOAT_EQ(world.camera->Vcam().ChargeNarrowDegrees(), 15.0f);
}

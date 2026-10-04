#include "Game/Level/FollowCamera.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/CameraTarget.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"

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
            state.charge.charge01 = 1.0f;
            state.framingHeld = framingHeld;
            state.charge.hasAimTarget = hasAim;
            state.charge.aimTargetCenter = aim;
            state.charge.aimTargetRadius = 0.5f;
            return state;
        }

        bool hasAim = false;
        NS::Core::Vector3 aim{};
        bool grounded = true;
        bool held = true;
        bool framingHeld = false;
    };

    struct FramingScene
    {
        NS::Obj::Scene scene;
        FramingTargetProbe* target = nullptr;
        NS::Game::Level::FollowCamera* camera = nullptr;

        FramingScene()
        {
            target =
                static_cast<FramingTargetProbe*>(scene.SpawnObject(std::make_unique<FramingTargetProbe>(), "target"));
            camera = scene.SpawnTransient<NS::Game::Level::FollowCamera>();
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

// 狙う相手のいる溜めでは、自機と相手の真ん中を画面の縦の中心へ寄せる。注視点は頭の高さにあるので、寄せないと
// 二人は画面の下の方に小さく写る。寄せは位置と注視点を同じだけ動かすので、カメラが下がる
TEST(FollowImpactFraming, ChargeWithATargetCentersThePairVertically)
{
    FramingScene world;
    world.target->hasAim = true;
    world.target->aim = NS::Core::Vector3{0.0f, 0.2f, 3.0f};
    world.Run(120);
    const NS::Obj::CameraPose pose = world.camera->Vcam().EvaluatePose(1.0f);
    NS::Core::Vector3 forward = pose.target - pose.position;
    forward.Normalize();
    NS::Core::Vector3 right = NS::Core::Cross(NS::Core::Vector3{0.0f, 1.0f, 0.0f}, forward);
    right.Normalize();
    const NS::Core::Vector3 up = NS::Core::Cross(forward, right);
    const NS::Core::Vector3 middle = (world.target->Root().Position() + world.target->aim) * 0.5f;
    EXPECT_NEAR(NS::Core::Dot(middle - pose.position, up), 0.0f, 0.02f);
}

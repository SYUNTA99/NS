#include "Game/Level/FollowCamera.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/CameraTarget.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/CameraManager.h"
#include "Runtime/Object/Components/CameraModifier.h"
#include "Runtime/Object/IUseCamera.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Scene/Scene.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <type_traits>

static_assert(!std::is_base_of_v<NS::Obj::Component, NS::Obj::CameraManager>);
static_assert(!std::is_base_of_v<NS::Obj::Component, NS::Obj::CameraComponent>);

TEST(CameraManager, SceneOwnsCameraWithoutActorHost)
{
    NS::Obj::Scene scene;
    ASSERT_NE(scene.MainCamera(), nullptr);
    EXPECT_EQ(scene.Objects().ObjectCount(), 0u);
}

// カメラの効果をモディファイアの積み重ねで掛けることと、カメラの窓口からの積み方を縛る

namespace
{
    NS::Obj::CameraShakeDesc ShakeOf(int frames)
    {
        return NS::Obj::CameraShakeDesc{
            .sideAmplitude = 0.2f, .upAmplitude = 0.1f, .frames = frames, .longestFlipFrames = 1};
    }

    // 決まった姿勢を返す仮想カメラ
    class FixedCamera final : public NS::Obj::VirtualCamera
    {
    public:
        FixedCamera() noexcept : NS::Obj::VirtualCamera() {}
        [[nodiscard]] NS::Obj::CameraPose EvaluatePose(float alpha) const noexcept override
        {
            (void)alpha;
            return MakePose(NS::Core::Vector3{1.0f, 0.0f, -5.0f},
                            NS::Core::Vector3{1.0f, 0.0f, 0.0f},
                            NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        }
        NS_REFLECT_NONE(FixedCamera, NS::Obj::VirtualCamera)
    };

    class FixedCameraHost final : public NS::Obj::Actor
    {
    public:
        FixedCameraHost() { AttachFixedComponent(vcam); }
        void ForEachPart(const PartVisitor& visitor) const override
        {
            NS::Obj::Actor::ForEachPart(visitor);
            visitor("Vcam", vcam);
        }
        mutable FixedCamera vcam;
    };

    class OrderProbe final : public NS::Obj::CameraModifier
    {
    public:
        OrderProbe(int order, float shift) noexcept : m_order(order), m_shift(shift) {}
        [[nodiscard]] int Order() const noexcept override { return m_order; }
        void Modify(NS::Obj::CameraPose& pose, const NS::Obj::CameraAxes& axes) const noexcept override
        {
            (void)axes;
            // 掛ける順で結果が変わる式。先に x を 2 倍し、後で足すと順が分かる
            pose.position.x = pose.position.x * 2.0f + m_shift;
        }
        [[nodiscard]] bool IsFinished() const noexcept override { return false; }

    private:
        void Advance() noexcept override {}
        int m_order = 0;
        float m_shift = 0.0f;
    };
} // namespace

TEST(CameraModifier, ShakeDrawsFirstFrameThenDecaysAndFinishes)
{
    std::unique_ptr<NS::Obj::CameraShakeModifier> shake = NS::Obj::CameraShakeModifier::Create(ShakeOf(3), 1.0f);
    ASSERT_NE(shake, nullptr);
    const NS::Core::Vector2 first = shake->Offset();
    EXPECT_FLOAT_EQ(std::abs(first.x), 0.2f);
    // 縦の最初の振れは下
    EXPECT_LT(first.y, 0.0f);

    // 積んだ直後の Tick は進めない
    shake->Tick();
    EXPECT_EQ(shake->Offset(), first);
    shake->Tick();
    EXPECT_LT(std::abs(shake->Offset().x), 0.2f);
    shake->Tick();
    shake->Tick();
    EXPECT_TRUE(shake->IsFinished());
    EXPECT_EQ(shake->Offset(), (NS::Core::Vector2{0.0f, 0.0f}));
}

TEST(CameraModifier, BrokenDescIsRejected)
{
    EXPECT_EQ(NS::Obj::CameraShakeModifier::Create(ShakeOf(0), 1.0f), nullptr);
    EXPECT_EQ(NS::Obj::CameraZoomRollModifier::Create(NS::Obj::CameraZoomRollDesc{.zoom = 0.5f}, 1.0f), nullptr);
}

TEST(CameraModifier, ZoomRollHoldsThenReturnsToIdentity)
{
    std::unique_ptr<NS::Obj::CameraZoomRollModifier> zoom = NS::Obj::CameraZoomRollModifier::Create(
        NS::Obj::CameraZoomRollDesc{.zoom = 1.2f, .rollDegrees = 3.0f, .holdFrames = 2, .returnFrames = 2}, -1.0f);
    ASSERT_NE(zoom, nullptr);
    EXPECT_FLOAT_EQ(zoom->Current().zoom, 1.2f);
    // 向きの符号が傾きに掛かる
    EXPECT_FLOAT_EQ(zoom->Current().rollDegrees, -3.0f);
    for (int i = 0; i < 5; ++i)
    {
        zoom->Tick();
    }
    EXPECT_TRUE(zoom->IsFinished());
    EXPECT_FLOAT_EQ(zoom->Current().zoom, 1.0f);
    EXPECT_FLOAT_EQ(zoom->Current().rollDegrees, 0.0f);
}

TEST(CameraManager, SameKindReplacesAndClearRemovesAll)
{
    NS::Obj::CameraManager cameras;
    EXPECT_TRUE(cameras.StartShake(ShakeOf(10)));
    EXPECT_TRUE(cameras.StartShake(ShakeOf(4)));
    // 揺れは 1 つだけ残り、後から積んだ設定になる
    const NS::Obj::CameraShakeModifier* shake = cameras.FindModifier<NS::Obj::CameraShakeModifier>();
    ASSERT_NE(shake, nullptr);
    EXPECT_TRUE(cameras.StartZoomRoll(NS::Obj::CameraZoomRollDesc{.zoom = 1.1f, .holdFrames = 3}));
    EXPECT_FLOAT_EQ(cameras.ZoomRoll().zoom, 1.1f);

    cameras.ClearModifiers();
    EXPECT_EQ(cameras.FindModifier<NS::Obj::CameraShakeModifier>(), nullptr);
    EXPECT_FLOAT_EQ(cameras.ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(cameras.ShakeOffset(), (NS::Core::Vector2{0.0f, 0.0f}));
}

TEST(CameraManager, FinishedModifiersAreRemovedOnUpdate)
{
    NS::Obj::CameraManager cameras;
    ASSERT_TRUE(cameras.StartShake(ShakeOf(1)));
    cameras.OnUpdate(); // 積んだ直後は進めない
    EXPECT_NE(cameras.FindModifier<NS::Obj::CameraShakeModifier>(), nullptr);
    cameras.OnUpdate();
    EXPECT_EQ(cameras.FindModifier<NS::Obj::CameraShakeModifier>(), nullptr);
}

TEST(CameraManager, ModifiersApplyInOrder)
{
    FixedCameraHost host;
    FixedCamera* vcam = &host.vcam;
    NS::Obj::CameraManager cameras;
    cameras.AddVirtualCamera(vcam);
    // 積んだ順と逆でも Order の小さい方が先に掛かる
    ASSERT_TRUE(cameras.AddModifier(std::make_unique<OrderProbe>(200, 1.0f)));
    ASSERT_TRUE(cameras.AddModifier(std::make_unique<OrderProbe>(100, 10.0f)));
    const std::optional<NS::Obj::CameraPose> pose = cameras.ComposePose(1.0f);
    ASSERT_TRUE(pose.has_value());
    // Order 100 (x*2+10) が先、200 (x*2+1) が後: (1*2+10)*2+1 = 25
    EXPECT_FLOAT_EQ(pose->position.x, 25.0f);
}

TEST(IUseCamera, ActorReachesSceneCameraManager)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* actor = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->GetCameraManager(), scene.GetCameraManager());
    EXPECT_TRUE(NS::Obj::StartCameraShake(*actor, ShakeOf(5)));
    EXPECT_NE(scene.GetCameraManager()->FindModifier<NS::Obj::CameraShakeModifier>(), nullptr);
    NS::Obj::StopCameraEffects(*actor);
    EXPECT_EQ(scene.GetCameraManager()->FindModifier<NS::Obj::CameraShakeModifier>(), nullptr);

    // シーンに居ない Actor は窓口が何もしない
    NS::Obj::Actor loose;
    EXPECT_EQ(loose.GetCameraManager(), nullptr);
    EXPECT_FALSE(NS::Obj::StartCameraShake(loose, ShakeOf(5)));
}

namespace
{
    class FollowTargetProbe final : public NS::Obj::Actor, public NS::Obj::ICameraTarget
    {
    public:
        const NS::Obj::ICameraTarget* GetCameraTarget() const noexcept override { return this; }
        NS::Obj::CameraTargetState GetCameraTargetState() const noexcept override
        {
            NS::Obj::CameraTargetState state;
            state.grounded = true;
            state.hasCharge = true;
            state.charge.held = true;
            state.charge.charge01 = 1.0f;
            return state;
        }
    };
} // namespace

TEST(FollowCamera, ActorFeedsItsFixedCameraBeforeEvaluatingIt)
{
    NS::Obj::Scene scene;
    NS::Obj::Actor* target = scene.SpawnObject(std::make_unique<FollowTargetProbe>(), "target");
    NS::Game::Level::FollowCamera* actor = scene.SpawnTransient<NS::Game::Level::FollowCamera>();
    NS::Obj::ThirdPersonFollow& vcam = actor->Vcam();
    vcam.SetActive(true);
    NS::Obj::ApplyJsonFields(vcam, nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}}});
    actor->Update();
    EXPECT_GT(vcam.ChargeNarrowDegrees(), 0.0f);
    actor->Kill();
    EXPECT_FLOAT_EQ(vcam.ChargeNarrowDegrees(), 0.0f);
    actor->Appear();
    actor->Update();
    EXPECT_GT(vcam.ChargeNarrowDegrees(), 0.0f);
    scene.DestroyObject(target->Id());
    actor->Update();
}

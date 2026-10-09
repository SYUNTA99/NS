#include "Game/Level/FollowCamera.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/CameraTarget.h"
#include "NSlib/Object/SubObjects/CameraManager.h"
#include "NSlib/Object/SubObjects/CameraModifier.h"
#include "NSlib/Object/IUse/IUseCamera.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneCamera.h"
#include "NSlib/Windows/Clock.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <type_traits>

static_assert(!std::is_base_of_v<NS::Obj::SubObject, NS::Obj::CameraManager>);
static_assert(!std::is_base_of_v<NS::Obj::SubObject, NS::Obj::SceneCamera>);
// 実カメラは遊びの向きを答えない。向きの口は IUseCamera の補助関数 1 本だけ
template <class T>
concept AnswersForwardHorizontal = requires(const T& camera) { camera.ForwardHorizontal(); };
static_assert(AnswersForwardHorizontal<NS::Obj::CameraManager>);
static_assert(!AnswersForwardHorizontal<NS::Obj::SceneCamera>);

TEST(CameraManager, SceneOwnsCameraWithoutActorHost)
{
    NS::Obj::Scene scene;
    ASSERT_NE(scene.MainCamera(), nullptr);
    EXPECT_EQ(scene.Objects().ObjectCount(), 0u);
}

// 管理役はカメラの段の登録物。段の表を回すだけで仮想カメラを選び、シーンに直書きの呼び出しは要らない
TEST(CameraManager, CameraPhaseSelectsTheVirtualCamera)
{
    NS::Obj::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{}), nullptr);
    NS::Obj::CameraManager* cameras = scene.GetCameraManager();
    ASSERT_NE(cameras, nullptr);
    ASSERT_EQ(cameras->ActiveVirtualCamera(), nullptr);

    scene.Objects().ExecutePhase(NS::Obj::UpdatePhase::Camera);

    EXPECT_NE(cameras->ActiveVirtualCamera(), nullptr);
}

// カメラの効果をモディファイアの積み重ねで掛けることと、カメラの窓口からの積み方を縛る

namespace
{
    NS::Obj::CameraShakeDesc ShakeOf(int frames)
    {
        return NS::Obj::CameraShakeDesc{
            .sideAmplitude = 0.2f, .upAmplitude = 0.1f, .frames = frames, .longestFlipFrames = 1};
    }

    // 割合が 0 なら +Z、1 なら +X 寄りを見る仮想カメラ。描画の割合が遊びの向きへ漏れると分かる
    class AlphaCamera final : public NS::Obj::VirtualCamera
    {
    public:
        [[nodiscard]] NS::Obj::CameraPose EvaluatePose(float alpha) const noexcept override
        {
            return MakePose(NS::Vector3{0.0f, 0.0f, -5.0f},
                            NS::Vector3{10.0f * alpha, 0.0f, 0.0f},
                            NS::Vector3{0.0f, 1.0f, 0.0f});
        }
        NS_REFLECT_NONE(AlphaCamera, NS::Obj::VirtualCamera)
    };

    class AlphaCameraHost final : public NS::Obj::Actor
    {
    public:
        AlphaCameraHost() { AttachFixedSubObject(vcam); }
        void ForEachSubObj(const SubObjVisitor& visitor) const override
        {
            NS::Obj::Actor::ForEachSubObj(visitor);
            visitor("Vcam", vcam);
        }
        mutable AlphaCamera vcam;
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
    const NS::Vector2 first = shake->Offset();
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
    EXPECT_EQ(shake->Offset(), (NS::Vector2{0.0f, 0.0f}));
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
    EXPECT_EQ(cameras.ShakeOffset(), (NS::Vector2{0.0f, 0.0f}));
}

TEST(CameraManager, FinishedModifiersAreRemovedOnTick)
{
    NS::Obj::CameraManager cameras;
    ASSERT_TRUE(cameras.StartShake(ShakeOf(1)));
    cameras.OnTick(); // 積んだ直後は進めない
    EXPECT_NE(cameras.FindModifier<NS::Obj::CameraShakeModifier>(), nullptr);
    cameras.OnTick();
    EXPECT_EQ(cameras.FindModifier<NS::Obj::CameraShakeModifier>(), nullptr);
}

TEST(CameraManager, ModifiersApplyInOrder)
{
    TestViewCameraHost host;
    host.Vcam().SetPose(NS::Vector3{1.0f, 0.0f, -5.0f}, NS::Vector3{1.0f, 0.0f, 0.0f});
    NS::Obj::CameraManager cameras;
    cameras.AddVirtualCamera(&host.Vcam());
    // 積んだ順と逆でも Order の小さい方が先に掛かる
    ASSERT_TRUE(cameras.AddModifier(std::make_unique<OrderProbe>(200, 1.0f)));
    ASSERT_TRUE(cameras.AddModifier(std::make_unique<OrderProbe>(100, 10.0f)));
    const std::optional<NS::Obj::CameraPose> pose = cameras.ComposePose(1.0f);
    ASSERT_TRUE(pose.has_value());
    // Order 100 (x*2+10) が先、200 (x*2+1) が後: (1*2+10)*2+1 = 25
    EXPECT_FLOAT_EQ(pose->position.x, 25.0f);
}

// 遊びの向きは仮想カメラから合成する。実カメラは描画の出力で、遊びは読まない
TEST(IUseCamera, ForwardFollowsTheVirtualCameraNotTheDrawnCamera)
{
    NS::Obj::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{}), nullptr);
    scene.MainCamera()->SetPosition(NS::Vector3{});
    scene.MainCamera()->SetTarget(NS::Vector3{1.0f, 0.0f, 0.0f});
    NS::Obj::Actor* actor = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(actor, nullptr);

    const NS::Vector3 forward = NS::Obj::CameraForwardHorizontal(*actor);

    EXPECT_FLOAT_EQ(forward.x, 0.0f);
    EXPECT_FLOAT_EQ(forward.z, 1.0f);
}

// 仮想カメラが無い時は管理役が無い時と同じ +Z。描いた実カメラの向きを拾わない
TEST(IUseCamera, ForwardWithoutAVirtualCameraIsPlusZ)
{
    NS::Obj::Scene scene;
    scene.MainCamera()->SetPosition(NS::Vector3{});
    scene.MainCamera()->SetTarget(NS::Vector3{1.0f, 0.0f, 0.0f});
    NS::Obj::Actor* actor = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(actor, nullptr);

    const NS::Vector3 forward = NS::Obj::CameraForwardHorizontal(*actor);

    EXPECT_FLOAT_EQ(forward.x, 0.0f);
    EXPECT_FLOAT_EQ(forward.z, 1.0f);
}

// 描画が割合 0 で実カメラを書いても、遊びの向きはその歩のブレンドの値 (割合 1) のまま
TEST(CameraManager, ForwardIgnoresTheDrawAlphaDuringABlend)
{
    NS::Obj::SceneCamera drawn;
    NS::Obj::CameraManager cameras;
    cameras.SetCamera(&drawn);
    // 1 歩で半分まで進むブレンド
    cameras.SetBlendDuration(NS::OS::FrameTimer::FixedDelta() * 2.0f);
    TestViewCameraHost from;
    cameras.AddVirtualCamera(&from.Vcam());
    cameras.OnTick();
    cameras.Evaluate(1.0f);
    AlphaCameraHost to;
    to.vcam.SetVcamPriority(1);
    cameras.AddVirtualCamera(&to.vcam);
    cameras.OnTick();
    cameras.Evaluate(0.0f);

    const std::optional<NS::Obj::CameraPose> view = cameras.ViewPose();
    ASSERT_TRUE(view.has_value());
    NS::Vector3 expected{};
    ASSERT_TRUE(NS::TryNormalizeHorizontal(view->target - view->position, expected));
    ASSERT_GT(expected.x, 0.1f);
    const NS::Vector3 forward = cameras.ForwardHorizontal();
    EXPECT_NEAR(forward.x, expected.x, 1e-5f);
    EXPECT_NEAR(forward.z, expected.z, 1e-5f);
}

// 効果は描く絵にだけ掛かる。揺れや傾きの間も、入力と狙いが読む向きは揺らさない
TEST(CameraManager, ForwardLeavesOutTheModifiers)
{
    NS::Obj::SceneCamera drawn;
    NS::Obj::CameraManager cameras;
    cameras.SetCamera(&drawn);
    TestViewCameraHost host;
    cameras.AddVirtualCamera(&host.Vcam());
    // 位置の x を 0*2+10 = 10 へずらし、描く視線を斜めにする
    ASSERT_TRUE(cameras.AddModifier(std::make_unique<OrderProbe>(100, 10.0f)));
    cameras.Evaluate(1.0f);
    ASSERT_FLOAT_EQ(drawn.Position().x, 10.0f);

    const NS::Vector3 forward = cameras.ForwardHorizontal();

    EXPECT_FLOAT_EQ(forward.x, 0.0f);
    EXPECT_FLOAT_EQ(forward.z, 1.0f);
    const std::optional<NS::Obj::CameraPose> view = cameras.ViewPose();
    ASSERT_TRUE(view.has_value());
    EXPECT_FLOAT_EQ(view->position.x, 0.0f);
}

// 窓口の視点も効果の前。揺れの間も位置が揺れない
TEST(IUseCamera, ViewPoseLeavesOutTheShake)
{
    NS::Obj::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{}), nullptr);
    NS::Obj::Actor* actor = scene.SpawnTransient<NS::Obj::Actor>();
    ASSERT_NE(actor, nullptr);
    ASSERT_TRUE(NS::Obj::StartCameraShake(*actor, ShakeOf(5)));
    const std::optional<NS::Obj::CameraPose> drawn = scene.GetCameraManager()->ComposePose(1.0f);
    ASSERT_TRUE(drawn.has_value());
    ASSERT_NE(drawn->position.x, 0.0f);

    const std::optional<NS::Obj::CameraPose> view = NS::Obj::CameraViewPose(*actor);

    ASSERT_TRUE(view.has_value());
    EXPECT_FLOAT_EQ(view->position.x, 0.0f);
    EXPECT_FLOAT_EQ(view->position.y, 0.0f);
    EXPECT_FLOAT_EQ(view->position.z, -5.0f);
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

// 追従カメラは出荷の姿で生まれる。プレイ中かは世界の駆動が答え、部品の active へ写さない
TEST(FollowCamera, VcamIsLiveFromConstruction)
{
    const NS::Game::Level::FollowCamera camera;
    EXPECT_TRUE(camera.Vcam().IsActiveSelf());
}

// 世界が回っている間は、描画の入口が管理役の姿勢を実カメラへ書く。描画先が無くても書く
TEST(SceneCameraOnRender, RunningWorldWritesTheFollowPoseOnRender)
{
    NS::Obj::Scene scene;
    NS::Game::Level::FollowCamera* follow = scene.SpawnTransient<NS::Game::Level::FollowCamera>();
    ASSERT_NE(follow, nullptr);
    // 追う相手の居ない追従カメラは (0, 0, -5) から原点を見る
    const NS::Obj::CameraPose expected = follow->Vcam().EvaluatePose(1.0f);
    scene.MainCamera()->SetPosition(NS::Vector3{100.0f, 0.0f, 0.0f});
    ASSERT_NE(expected.position.x, 100.0f);

    scene.OnRender();

    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().x, expected.position.x);
    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().y, expected.position.y);
    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().z, expected.position.z);
}

// 世界を止めた側が実カメラを書く。止めている間に管理役が書くと、エディタの視点を追従カメラが奪う
TEST(SceneCameraOnRender, StoppedWorldLeavesTheRealCameraToWhoeverStoppedIt)
{
    NS::Obj::Scene scene;
    ASSERT_NE(scene.SpawnTransient<NS::Game::Level::FollowCamera>(), nullptr);
    scene.SetSimulationEnabled(false);
    scene.MainCamera()->SetPosition(NS::Vector3{100.0f, 0.0f, 0.0f});

    scene.OnRender();

    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().x, 100.0f);
    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().y, 0.0f);
    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().z, 0.0f);
}

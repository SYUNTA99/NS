#include "Game/Level/FollowCameraActor.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Windows/Clock.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <type_traits>

// 実カメラは遊びの向きを答えない。向きの口は IUseCamera の補助関数 1 本だけ
template <class T>
concept AnswersForwardHorizontal = requires(const T& camera) { camera.ForwardHorizontal(); };
static_assert(AnswersForwardHorizontal<NS::CameraManager>);
static_assert(!AnswersForwardHorizontal<NS::SceneCamera>);

TEST(CameraManager, SceneOwnsCameraWithoutActorHost)
{
    NS::Scene scene;
    ASSERT_NE(scene.MainCamera(), nullptr);
    EXPECT_EQ(scene.Objects().ObjectCount(), 0u);
}

// 管理役はカメラの段の登録物。段の表を回すだけで仮想カメラを選び、シーンに直書きの呼び出しは要らない
TEST(CameraManager, CameraPhaseSelectsTheVirtualCamera)
{
    NS::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{}), nullptr);
    NS::CameraManager* cameras = scene.GetCameraManager();
    ASSERT_NE(cameras, nullptr);
    ASSERT_EQ(cameras->ActiveVirtualCamera(), nullptr);

    scene.ExecutePhase(NS::UpdatePhase::Camera);

    EXPECT_NE(cameras->ActiveVirtualCamera(), nullptr);
}

// カメラの効果をモディファイアの積み重ねで掛けることと、カメラの窓口からの積み方を縛る

namespace
{
    NS::CameraShakeDesc ShakeOf(int frames)
    {
        return NS::CameraShakeDesc{
            .sideAmplitude = 0.2f, .upAmplitude = 0.1f, .frames = frames, .longestFlipFrames = 1};
    }

    // 割合が 0 なら +Z、1 なら +X 寄りを見る仮想カメラ。描画の割合が遊びの向きへ漏れると分かる
    class AlphaCamera final : public NS::VirtualCamera
    {
    public:
        using NS::VirtualCamera::VirtualCamera;

        [[nodiscard]] NS::CameraPose EvaluatePose(float alpha) const noexcept override
        {
            return MakePose(
                NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{10.0f * alpha, 0.0f, 0.0f}, NS::Vector3{0.0f, 1.0f, 0.0f});
        }
        NS_REFLECT_NONE(AlphaCamera, NS::VirtualCamera)
    };

    class AlphaCameraHostActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(AlphaCameraHostActor, NS::SceneActor)
        NS_CLASSOBJ(vcam, "Vcam")
        NS_REFLECT_END()

        std::unique_ptr<AlphaCamera> vcam;

        void OnEndPlay() override
        {
            NS::SceneActor::OnEndPlay();
            NS::UnregisterVirtualCamera(this, vcam.get());
        }

    protected:
        void OnInit() override { CreateClassObj(vcam, this); }
        void OnStart() override
        {
            NS::SceneActor::OnStart();
            NS::RegisterVirtualCamera(this, vcam.get());
        }
    };

    class OrderProbe final : public NS::CameraModifier
    {
    public:
        OrderProbe(int order, float shift) noexcept : m_order(order), m_shift(shift) {}
        [[nodiscard]] int Order() const noexcept override { return m_order; }
        void Modify(NS::CameraPose& pose, const NS::CameraAxes& axes) const noexcept override
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
    std::unique_ptr<NS::CameraShakeModifier> shake = NS::CameraShakeModifier::Create(ShakeOf(3), 1.0f);
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
    EXPECT_EQ(NS::CameraShakeModifier::Create(ShakeOf(0), 1.0f), nullptr);
    EXPECT_EQ(NS::CameraZoomRollModifier::Create(NS::CameraZoomRollDesc{.zoom = 0.5f}, 1.0f), nullptr);
}

TEST(CameraModifier, ZoomRollHoldsThenReturnsToIdentity)
{
    std::unique_ptr<NS::CameraZoomRollModifier> zoom = NS::CameraZoomRollModifier::Create(
        NS::CameraZoomRollDesc{.zoom = 1.2f, .rollDegrees = 3.0f, .holdFrames = 2, .returnFrames = 2}, -1.0f);
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
    NS::CameraManager cameras;
    EXPECT_TRUE(cameras.StartShake(ShakeOf(10)));
    EXPECT_TRUE(cameras.StartShake(ShakeOf(4)));
    // 揺れは 1 つだけ残り、後から積んだ設定になる
    const NS::CameraShakeModifier* shake = cameras.FindModifier<NS::CameraShakeModifier>();
    ASSERT_NE(shake, nullptr);
    EXPECT_TRUE(cameras.StartZoomRoll(NS::CameraZoomRollDesc{.zoom = 1.1f, .holdFrames = 3}));
    EXPECT_FLOAT_EQ(cameras.ZoomRoll().zoom, 1.1f);

    cameras.ClearModifiers();
    EXPECT_EQ(cameras.FindModifier<NS::CameraShakeModifier>(), nullptr);
    EXPECT_FLOAT_EQ(cameras.ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(cameras.ShakeOffset(), (NS::Vector2{0.0f, 0.0f}));
}

TEST(CameraManager, FinishedModifiersAreRemovedOnTick)
{
    NS::CameraManager cameras;
    ASSERT_TRUE(cameras.StartShake(ShakeOf(1)));
    cameras.OnTick(); // 積んだ直後は進めない
    EXPECT_NE(cameras.FindModifier<NS::CameraShakeModifier>(), nullptr);
    cameras.OnTick();
    EXPECT_EQ(cameras.FindModifier<NS::CameraShakeModifier>(), nullptr);
}

TEST(CameraManager, ModifiersApplyInOrder)
{
    const std::unique_ptr<TestViewCameraHostActor> hostOwned = NS::CreateActor<TestViewCameraHostActor>();
    TestViewCameraHostActor& host = *hostOwned;
    host.Vcam().SetPose(NS::Vector3{1.0f, 0.0f, -5.0f}, NS::Vector3{1.0f, 0.0f, 0.0f});
    NS::CameraManager cameras;
    cameras.AddVirtualCamera(&host.Vcam());
    // 積んだ順と逆でも Order の小さい方が先に掛かる
    ASSERT_TRUE(cameras.AddModifier(std::make_unique<OrderProbe>(200, 1.0f)));
    ASSERT_TRUE(cameras.AddModifier(std::make_unique<OrderProbe>(100, 10.0f)));
    const std::optional<NS::CameraPose> pose = cameras.ComposePose(1.0f);
    ASSERT_TRUE(pose.has_value());
    // Order 100 (x*2+10) が先、200 (x*2+1) が後: (1*2+10)*2+1 = 25
    EXPECT_FLOAT_EQ(pose->position.x, 25.0f);
}

// 遊びの向きは仮想カメラから合成する。実カメラは描画の出力で、遊びは読まない
TEST(IUseCamera, ForwardFollowsTheVirtualCameraNotTheDrawnCamera)
{
    NS::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{}), nullptr);
    scene.MainCamera()->SetPosition(NS::Vector3{});
    scene.MainCamera()->SetTarget(NS::Vector3{1.0f, 0.0f, 0.0f});
    NS::SceneActor* actor = scene.SpawnObject<NS::SceneActor>("SceneActor");
    ASSERT_NE(actor, nullptr);

    const NS::Vector3 forward = NS::CalcCameraForwardHorizontal(actor);

    EXPECT_FLOAT_EQ(forward.x, 0.0f);
    EXPECT_FLOAT_EQ(forward.z, 1.0f);
}

// 仮想カメラが無い時は管理役が無い時と同じ +Z。描いた実カメラの向きを拾わない
TEST(IUseCamera, ForwardWithoutAVirtualCameraIsPlusZ)
{
    NS::Scene scene;
    scene.MainCamera()->SetPosition(NS::Vector3{});
    scene.MainCamera()->SetTarget(NS::Vector3{1.0f, 0.0f, 0.0f});
    NS::SceneActor* actor = scene.SpawnObject<NS::SceneActor>("SceneActor");
    ASSERT_NE(actor, nullptr);

    const NS::Vector3 forward = NS::CalcCameraForwardHorizontal(actor);

    EXPECT_FLOAT_EQ(forward.x, 0.0f);
    EXPECT_FLOAT_EQ(forward.z, 1.0f);
}

// 描画が割合 0 で実カメラを書いても、遊びの向きはその歩のブレンドの値 (割合 1) のまま
TEST(CameraManager, ForwardIgnoresTheDrawAlphaDuringABlend)
{
    NS::SceneCamera drawn;
    NS::CameraManager cameras;
    cameras.SetCamera(&drawn);
    // 1 歩で半分まで進むブレンド
    cameras.SetBlendDuration(NS::OS::FrameTimer::FixedDelta() * 2.0f);
    const std::unique_ptr<TestViewCameraHostActor> fromOwned = NS::CreateActor<TestViewCameraHostActor>();
    TestViewCameraHostActor& from = *fromOwned;
    cameras.AddVirtualCamera(&from.Vcam());
    cameras.OnTick();
    cameras.Evaluate(1.0f);
    const std::unique_ptr<AlphaCameraHostActor> toOwned = NS::CreateActor<AlphaCameraHostActor>();
    AlphaCameraHostActor& to = *toOwned;
    to.vcam->SetVcamPriority(1);
    cameras.AddVirtualCamera(to.vcam.get());
    cameras.OnTick();
    cameras.Evaluate(0.0f);

    const std::optional<NS::CameraPose> view = cameras.ViewPose();
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
    NS::SceneCamera drawn;
    NS::CameraManager cameras;
    cameras.SetCamera(&drawn);
    const std::unique_ptr<TestViewCameraHostActor> hostOwned = NS::CreateActor<TestViewCameraHostActor>();
    TestViewCameraHostActor& host = *hostOwned;
    cameras.AddVirtualCamera(&host.Vcam());
    // 位置の x を 0*2+10 = 10 へずらし、描く視線を斜めにする
    ASSERT_TRUE(cameras.AddModifier(std::make_unique<OrderProbe>(100, 10.0f)));
    cameras.Evaluate(1.0f);
    ASSERT_FLOAT_EQ(drawn.Position().x, 10.0f);

    const NS::Vector3 forward = cameras.ForwardHorizontal();

    EXPECT_FLOAT_EQ(forward.x, 0.0f);
    EXPECT_FLOAT_EQ(forward.z, 1.0f);
    const std::optional<NS::CameraPose> view = cameras.ViewPose();
    ASSERT_TRUE(view.has_value());
    EXPECT_FLOAT_EQ(view->position.x, 0.0f);
}

// 窓口の視点も効果の前。揺れの間も位置が揺れない
TEST(IUseCamera, ViewPoseLeavesOutTheShake)
{
    NS::Scene scene;
    ASSERT_NE(PlaceViewCamera(scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{}), nullptr);
    NS::SceneActor* actor = scene.SpawnObject<NS::SceneActor>("SceneActor");
    ASSERT_NE(actor, nullptr);
    ASSERT_TRUE(NS::StartCameraShake(actor, ShakeOf(5)));
    const std::optional<NS::CameraPose> drawn = scene.GetCameraManager()->ComposePose(1.0f);
    ASSERT_TRUE(drawn.has_value());
    ASSERT_NE(drawn->position.x, 0.0f);

    const std::optional<NS::CameraPose> view = NS::GetCameraViewPose(actor);

    ASSERT_TRUE(view.has_value());
    EXPECT_FLOAT_EQ(view->position.x, 0.0f);
    EXPECT_FLOAT_EQ(view->position.y, 0.0f);
    EXPECT_FLOAT_EQ(view->position.z, -5.0f);
}

TEST(IUseCamera, ActorReachesSceneCameraManager)
{
    NS::Scene scene;
    NS::SceneActor* actor = scene.SpawnObject<NS::SceneActor>("SceneActor");
    ASSERT_NE(actor, nullptr);
    EXPECT_EQ(actor->GetCameraManager(), scene.GetCameraManager());
    EXPECT_TRUE(NS::StartCameraShake(actor, ShakeOf(5)));
    EXPECT_NE(scene.GetCameraManager()->FindModifier<NS::CameraShakeModifier>(), nullptr);
    NS::StopCameraEffects(actor);
    EXPECT_EQ(scene.GetCameraManager()->FindModifier<NS::CameraShakeModifier>(), nullptr);

    // シーンに居ない SceneActor は窓口が何もしない
    NS::SceneActor loose;
    EXPECT_EQ(loose.GetCameraManager(), nullptr);
    EXPECT_FALSE(NS::StartCameraShake(&loose, ShakeOf(5)));
}

// 追従カメラの個体は、欄「追従対象」で指した自機の溜めを読んで仮想カメラへ渡す
TEST(FollowCameraActor, ActorFeedsItsFixedCameraBeforeEvaluatingIt)
{
    NS::Scene scene;
    PlayerActor* player = scene.SpawnObject<PlayerActor>("player");
    ASSERT_NE(player, nullptr);
    ASSERT_EQ(NS::ApplyJsonFields(player->Params(), {{"チャージしきい値秒", 0.2f}, {"チャージ満タン秒", 1.0f}}), 0u);
    GL::Level::FollowCameraActor* actor = scene.SpawnObject<GL::Level::FollowCameraActor>("FollowCameraActor");
    NS::ThirdPersonFollow& vcam = actor->Vcam();
    NS::ApplyJsonFields(vcam, nlohmann::json{{"追従対象", nlohmann::json{{"ref", player->Id()}}}});
    // しきい値を越えるまで押し続ける
    for (int held = 0; held < 30; ++held)
    {
        player->Update(true);
        actor->Update();
    }
    ASSERT_TRUE(player->ChargeJudge().IsHoldingCharge());
    EXPECT_GT(vcam.ChargeNarrowDegrees(), 0.0f);
    actor->Disappear();
    EXPECT_FLOAT_EQ(vcam.ChargeNarrowDegrees(), 0.0f);
    actor->Appear();
    player->Update(true);
    actor->Update();
    EXPECT_GT(vcam.ChargeNarrowDegrees(), 0.0f);
    scene.DestroyObject(player->Id());
    actor->Update();
}

// 世界が回っている間は、描画の入口が管理役の姿勢を実カメラへ書く。描画先が無くても書く
TEST(SceneCameraOnRender, RunningWorldWritesTheFollowPoseOnRender)
{
    NS::Scene scene;
    GL::Level::FollowCameraActor* follow = scene.SpawnObject<GL::Level::FollowCameraActor>("FollowCameraActor");
    ASSERT_NE(follow, nullptr);
    // 追う相手の居ない追従カメラは (0, 0, -5) から原点を見る
    const NS::CameraPose expected = follow->Vcam().EvaluatePose(1.0f);
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
    NS::Scene scene;
    ASSERT_NE(scene.SpawnObject<GL::Level::FollowCameraActor>("FollowCameraActor"), nullptr);
    scene.SetSimulationEnabled(false);
    scene.MainCamera()->SetPosition(NS::Vector3{100.0f, 0.0f, 0.0f});

    scene.OnRender();

    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().x, 100.0f);
    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().y, 0.0f);
    EXPECT_FLOAT_EQ(scene.MainCamera()->Position().z, 0.0f);
}

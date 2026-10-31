#include "Game/Level/SensorKinds.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Window.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"
#include "NSlib/CoreObject/IGet/IGetSceneSystem.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitScreenDirector.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/Render/EffectScene.h"
#include "Tests/TestViewCamera.h"

#include <gtest/gtest.h>

#include <optional>

// 普段の歩・シーンが止まっている歩・世界を進めない歩で、Scene のメンバーのどれが回ったかを、メンバーの効果で見る
// 物理の管理役は broadphase の木を組み直すだけで、問いの答えも body の位置も変えないので、ここでは見ない。
// UI の管理役は開いている画面の OnTick を呼ぶだけなので、同じ実時間で進む暗転で見る

namespace
{
    // 進め続ける歩の数。stream_life10 の板の寿命 10 フレームより長い
    constexpr int k_ObservedSteps = 30;

    // 重なりが続いた知らせを数える。センサーの管理役が回った歩だけ増える
    class SensorStayActor final : public NS::SceneActor
    {
    public:
        void OnSensorStay(NS::HitSensor* self, NS::HitSensor* other) override
        {
            (void)self;
            (void)other;
            ++stays;
        }

        int stays = 0;

    protected:
        void OnInit() override
        {
            CreateClassObj(m_hitSensors);
            NS::HitSensor* body = m_hitSensors->AddShape(this, "Body", GL::Level::SensorKind::Area, NS::HitSensorShape::Sphere);
            NS::Cast<NS::ShapeHitSensor>(body)->SetSphere(1.0f);
        }
    };

    // 歩ごとに変わるメンバーの値を、1 つの時点で控えた物
    struct StepView
    {
        float fadeAlpha = 0.0f;
        float cameraX = 0.0f;
        bool effectExists = false;
        int sensorStays = 0;
        int flashRemaining = 0;
        float padLeft = 0.0f;
    };

    // 全部のメンバーに、歩ごとに進む物を 1 つずつ始めたシーン
    class StepScene
    {
    public:
        StepScene()
            : m_window(NS::OS::WindowDesc{.title = "歩の試し", .size = {32, 32}, .visible = false}),
              m_device(NS::Gfx::D3DDeviceDesc{}, m_window)
        {
            m_scene.SetDevice(&m_device);
        }

        //! 描画の装置が作れた場合 true。作れなければエフェクトの世界が無い
        [[nodiscard]] bool IsValid() const noexcept { return m_device.IsValid() && m_scene.GetEffectScene() != nullptr; }

        //! 全部のメンバーに進む物を始め、始めた歩の扱いが済むまで普段の歩で回す
        void Start()
        {
            holder = m_scene.SpawnActor<NS::SceneActor>("Holder");
            m_sensorA = m_scene.SpawnActor<SensorStayActor>("SensorA");
            m_sensorB = m_scene.SpawnActor<SensorStayActor>("SensorB");
            (void)PlaceViewCamera(m_scene, NS::Vector3{0.0f, 0.0f, -5.0f}, NS::Vector3{0.0f, 0.0f, 0.0f});

            // 2 秒かけて x = 10 から仮想カメラの x = 0 へ寄る
            NS::CameraManager* cameras = m_scene.GetCameraManager();
            cameras->SetBlendDuration(2.0f);
            NS::CameraPose from{};
            from.position = NS::Vector3{10.0f, 0.0f, -5.0f};
            cameras->BeginBlendFrom(from);

            m_scene.Fade().BeginOut(10.0f);

            const NS::Gfx::EffectResourceDesc stream{
                "scene-step-stream",
                "stream_life10",
                NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Source/Tests/data/effects/stream_life10.efkefc"),
                1};
            NS::Gfx::EffectScene* effects = m_scene.GetEffectScene();
            ASSERT_TRUE(effects->Preload(stream));
            m_effect = effects->Play(stream, NS::Gfx::EffectPlayDesc{});
            ASSERT_TRUE(m_effect.IsValid());

            NS::HitScreenDirector* screen = NS::GetOrCreateSceneSystem<NS::HitScreenDirector>(&m_scene);
            ASSERT_NE(screen, nullptr);
            screen->StartFlash(holder, 1000, 1.0f);

            NS::PadRumbleDirector* pad = NS::GetOrCreateSceneSystem<NS::PadRumbleDirector>(&m_scene);
            ASSERT_NE(pad, nullptr);
            NS::HitPadVibration vibration;
            vibration.left.count = 2;
            vibration.left.keys[0] = NS::Curve::Key{0.0f, 1.0f};
            vibration.left.keys[1] = NS::Curve::Key{1000.0f, 0.0f};
            vibration.frames = 1000;
            pad->Start(holder, vibration);

            for (int i = 0; i < 3; ++i)
            {
                m_scene.OnUpdate();
            }
            // 板を新しく出すのをやめる。エフェクトの世界が進めば、出した板が寿命で消えて再生が終わる
            effects->StopRoot(m_effect);
        }

        void Step(int count)
        {
            for (int i = 0; i < count; ++i)
            {
                m_scene.OnUpdate();
            }
        }

        [[nodiscard]] StepView View() const
        {
            StepView view;
            view.fadeAlpha = m_scene.Fade().Alpha();
            const std::optional<NS::CameraPose> pose = m_scene.GetCameraManager()->ViewPose();
            if (pose.has_value())
            {
                view.cameraX = pose->position.x;
            }
            view.effectExists = m_scene.GetEffectScene()->Exists(m_effect);
            view.sensorStays = m_sensorA->stays + m_sensorB->stays;
            view.flashRemaining = NS::FindSceneSystem<NS::HitScreenDirector>(&m_scene)->FlashFramesRemaining();
            view.padLeft = NS::OS::Input::Get().Gamepad().Vibration().left;
            return view;
        }

        [[nodiscard]] NS::Scene& Scene() noexcept { return m_scene; }

        NS::SceneActor* holder = nullptr; // 止めを始めるアクター。画面の演出と振動を頼む物も兼ねる

    private:
        // シーンより先に宣言し、エフェクトの世界より後に壊す
        NS::OS::Window m_window;
        NS::Gfx::D3DDevice m_device;
        NS::Scene m_scene;
        SensorStayActor* m_sensorA = nullptr;
        SensorStayActor* m_sensorB = nullptr;
        NS::Gfx::EffectHandle m_effect{};
    };
} // namespace

// 普段の歩は、暗転・カメラ・エフェクト・センサー・画面の演出・振動の全部を進める
TEST(SceneStep, AWorldStepAdvancesEveryMember)
{
    StepScene world;
    ASSERT_TRUE(world.IsValid());
    ASSERT_NO_FATAL_FAILURE(world.Start());
    const StepView before = world.View();
    ASSERT_TRUE(before.effectExists);

    world.Step(k_ObservedSteps);
    const StepView after = world.View();

    EXPECT_GT(after.fadeAlpha, before.fadeAlpha);
    EXPECT_LT(after.cameraX, before.cameraX);
    EXPECT_FALSE(after.effectExists);
    EXPECT_GT(after.sensorStays, before.sensorStays);
    EXPECT_LT(after.flashRemaining, before.flashRemaining);
    EXPECT_LT(after.padLeft, before.padLeft);
}

// シーンが止まっている歩は、止め始めたアクターを見せる側 (暗転・カメラ・画面の演出・振動) だけを進め、
// エフェクトとセンサーを止める
TEST(SceneStep, AStoppedStepAdvancesOnlyWhatShowsTheMover)
{
    StepScene world;
    ASSERT_TRUE(world.IsValid());
    ASSERT_NO_FATAL_FAILURE(world.Start());
    world.Scene().HoldOthers(world.holder, k_ObservedSteps + 10);
    // 止めを頼んだ直後の歩は、止めが効き始める歩が変わっても同じに通るよう見ない
    world.Step(1);
    const StepView before = world.View();
    ASSERT_TRUE(before.effectExists);

    world.Step(k_ObservedSteps);
    const StepView after = world.View();

    ASSERT_TRUE(world.Scene().IsHoldingOthers());
    EXPECT_GT(after.fadeAlpha, before.fadeAlpha);
    EXPECT_LT(after.cameraX, before.cameraX);
    EXPECT_TRUE(after.effectExists);
    EXPECT_EQ(after.sensorStays, before.sensorStays);
    EXPECT_LT(after.flashRemaining, before.flashRemaining);
    EXPECT_LT(after.padLeft, before.padLeft);
}

// 世界を進めない歩 (世界の速さ 0) は、実時間で進む暗転だけを進める
TEST(SceneStep, AStepWithoutTheWorldAdvancesOnlyTheRealTimeMembers)
{
    StepScene world;
    ASSERT_TRUE(world.IsValid());
    ASSERT_NO_FATAL_FAILURE(world.Start());
    world.Scene().SetWorldSpeed(0.0f);
    const StepView before = world.View();
    ASSERT_TRUE(before.effectExists);

    world.Step(k_ObservedSteps);
    const StepView after = world.View();

    EXPECT_NEAR(after.fadeAlpha - before.fadeAlpha,
                static_cast<float>(k_ObservedSteps) * NS::OS::FrameTimer::FixedDelta() / 10.0f,
                1.0e-5f);
    EXPECT_FLOAT_EQ(after.cameraX, before.cameraX);
    EXPECT_TRUE(after.effectExists);
    EXPECT_EQ(after.sensorStays, before.sensorStays);
    EXPECT_EQ(after.flashRemaining, before.flashRemaining);
    EXPECT_FLOAT_EQ(after.padLeft, before.padLeft);
}

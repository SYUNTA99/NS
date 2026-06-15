#include <gtest/gtest.h>

#include <Framework/Core/Clock.h>
#include <Framework/Scene/Components/CameraBrainComponent.h>
#include <Framework/Scene/Components/CameraComponent.h>
#include <Framework/Scene/Components/PlacedVirtualCamera.h>
#include <Framework/Scene/Components/VirtualCameraComponent.h>
#include <Framework/Scene/GameObject.h>

namespace
{
    using NS::Scene::CameraBrainComponent;
    using NS::Scene::CameraComponent;
    using NS::Scene::CameraPose;
    using NS::Scene::GameObject;
    using NS::Scene::PlacedVirtualCamera;
    using NS::Scene::TickPriority;
    using NS::Scene::VirtualCameraComponent;

    constexpr float kDt = 1.0f / 60.0f;

    // 固定 pose を返すだけのテスト用 vcam。 X 座標だけずらして補間が見えるようにする
    class FixedVcam : public VirtualCameraComponent
    {
    public:
        explicit FixedVcam(float x) noexcept : VirtualCameraComponent(static_cast<int>(TickPriority::Camera)), m_x(x) {}
        [[nodiscard]] CameraPose EvaluatePose(float) const noexcept override
        {
            return MakePose({m_x, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
        }

    private:
        float m_x;
    };
} // namespace

class CameraBrainTest : public ::testing::Test
{
protected:
    void SetUp() override { NS::Core::FrameTimer::SetFixedDelta(kDt); }
};

TEST_F(CameraBrainTest, PoseLerpInterpolatesEachField)
{
    CameraPose a{};
    a.position = NS::Math::Vector3{0.0f, 0.0f, 0.0f};
    a.fovY = NS::Math::Radians{1.0f};
    a.farPlane = 100.0f;
    CameraPose b{};
    b.position = NS::Math::Vector3{10.0f, 0.0f, 0.0f};
    b.fovY = NS::Math::Radians{3.0f};
    b.farPlane = 200.0f;

    const auto mid = CameraPose::Lerp(a, b, 0.5f);
    EXPECT_FLOAT_EQ(mid.position.x, 5.0f);
    EXPECT_FLOAT_EQ(mid.fovY.value, 2.0f);
    EXPECT_FLOAT_EQ(mid.farPlane, 150.0f);
}

TEST_F(CameraBrainTest, SelectsHighestPriorityActiveVcam)
{
    GameObject host;
    auto* cam = host.AddComponent<CameraComponent>();
    auto* brain = host.AddComponent<CameraBrainComponent>();
    brain->SetCamera(cam);
    brain->SetBlendDuration(0.0f);

    GameObject hostLow;
    GameObject hostHigh;
    auto* low = hostLow.AddComponent<FixedVcam>(1.0f);
    auto* high = hostHigh.AddComponent<FixedVcam>(2.0f);
    low->SetVcamPriority(10);
    high->SetVcamPriority(20);
    brain->AddVirtualCamera(low);
    brain->AddVirtualCamera(high);

    brain->OnUpdate();
    brain->Evaluate(1.0f);

    EXPECT_EQ(brain->ActiveVirtualCamera(), high);
    EXPECT_FLOAT_EQ(cam->Position().x, 2.0f);
}

TEST_F(CameraBrainTest, InactiveVcamIsSkipped)
{
    GameObject host;
    auto* cam = host.AddComponent<CameraComponent>();
    auto* brain = host.AddComponent<CameraBrainComponent>();
    brain->SetCamera(cam);
    brain->SetBlendDuration(0.0f);

    GameObject hostA;
    GameObject hostB;
    auto* a = hostA.AddComponent<FixedVcam>(1.0f);
    auto* b = hostB.AddComponent<FixedVcam>(2.0f);
    a->SetVcamPriority(20); // 高優先だが休止
    a->SetActive(false);
    b->SetVcamPriority(10);
    brain->AddVirtualCamera(a);
    brain->AddVirtualCamera(b);

    brain->OnUpdate();
    brain->Evaluate(1.0f);

    EXPECT_EQ(brain->ActiveVirtualCamera(), b);
    EXPECT_FLOAT_EQ(cam->Position().x, 2.0f);
}

TEST_F(CameraBrainTest, BlendSweepsFromOldToNewOverDuration)
{
    GameObject host;
    auto* cam = host.AddComponent<CameraComponent>();
    auto* brain = host.AddComponent<CameraBrainComponent>();
    brain->SetCamera(cam);
    brain->SetBlendDuration(0.5f);

    GameObject hostA;
    GameObject hostB;
    auto* a = hostA.AddComponent<FixedVcam>(0.0f);
    auto* b = hostB.AddComponent<FixedVcam>(10.0f);
    a->SetVcamPriority(20);
    b->SetVcamPriority(10);
    brain->AddVirtualCamera(a);
    brain->AddVirtualCamera(b);

    // 初回は a が active (旧 pose 無し)。 カットで確定する
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(cam->Position().x, 0.0f);

    // b を優先に上げて切替 → ブレンド開始。 1 ステップ目はまだ a 寄り
    b->SetVcamPriority(30);
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_GT(cam->Position().x, 0.0f);
    EXPECT_LT(cam->Position().x, 10.0f);

    // duration を超えるまで進めると b に収束する
    for (int i = 0; i < 60; ++i)
    {
        brain->OnUpdate();
        brain->Evaluate(1.0f);
    }
    EXPECT_FLOAT_EQ(cam->Position().x, 10.0f);
}

TEST_F(CameraBrainTest, ZeroBlendDurationCutsInstantly)
{
    GameObject host;
    auto* cam = host.AddComponent<CameraComponent>();
    auto* brain = host.AddComponent<CameraBrainComponent>();
    brain->SetCamera(cam);
    brain->SetBlendDuration(0.0f);

    GameObject hostA;
    GameObject hostB;
    auto* a = hostA.AddComponent<FixedVcam>(0.0f);
    auto* b = hostB.AddComponent<FixedVcam>(10.0f);
    a->SetVcamPriority(20);
    b->SetVcamPriority(10);
    brain->AddVirtualCamera(a);
    brain->AddVirtualCamera(b);

    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(cam->Position().x, 0.0f);

    // 即時カット設定では切替の次フレームでもう新 vcam に乗っている
    b->SetVcamPriority(30);
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(cam->Position().x, 10.0f);
}

TEST_F(CameraBrainTest, PlacedVcamReturnsItsSetView)
{
    GameObject host;
    auto* placed = host.AddComponent<PlacedVirtualCamera>();
    placed->SetView({3.0f, 7.0f, -2.0f}, {1.0f, 0.0f, 4.0f});

    const auto pose = placed->EvaluatePose(1.0f);
    EXPECT_FLOAT_EQ(pose.position.x, 3.0f);
    EXPECT_FLOAT_EQ(pose.position.y, 7.0f);
    EXPECT_FLOAT_EQ(pose.position.z, -2.0f);
    EXPECT_FLOAT_EQ(pose.target.x, 1.0f);
    EXPECT_FLOAT_EQ(pose.target.z, 4.0f);
}

TEST_F(CameraBrainTest, ActivatingPlacedVcamBlendsTowardIt)
{
    GameObject host;
    auto* cam = host.AddComponent<CameraComponent>();
    auto* brain = host.AddComponent<CameraBrainComponent>();
    brain->SetCamera(cam);
    brain->SetBlendDuration(0.5f);

    GameObject followHost;
    GameObject areaHost;
    auto* follow = followHost.AddComponent<FixedVcam>(0.0f);
    auto* area = areaHost.AddComponent<PlacedVirtualCamera>();
    area->SetView({10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    follow->SetVcamPriority(0);
    area->SetVcamPriority(10); // active な間だけ follow を上回る
    area->SetActive(false);    // エリア外を想定
    brain->AddVirtualCamera(follow);
    brain->AddVirtualCamera(area);

    // エリア外: follow が選ばれる
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_EQ(brain->ActiveVirtualCamera(), follow);
    EXPECT_FLOAT_EQ(cam->Position().x, 0.0f);

    // エリア進入で placed を active 化 → ブレンドで近づく
    area->SetActive(true);
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_EQ(brain->ActiveVirtualCamera(), area);
    EXPECT_GT(cam->Position().x, 0.0f);
    EXPECT_LT(cam->Position().x, 10.0f);

    for (int i = 0; i < 60; ++i)
    {
        brain->OnUpdate();
        brain->Evaluate(1.0f);
    }
    EXPECT_FLOAT_EQ(cam->Position().x, 10.0f);
}

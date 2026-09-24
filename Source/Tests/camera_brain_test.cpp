#include <gtest/gtest.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Object/Components/CameraBrain.h>
#include <Runtime/Object/Components/CameraComponent.h>
#include <Runtime/Object/Components/VirtualCamera.h>
#include <Runtime/Object/GameObject.h>

namespace
{
    using NS::Obj::CameraBrain;
    using NS::Obj::CameraComponent;
    using NS::Obj::CameraPose;
    using NS::Obj::GameObject;
    using NS::Obj::TickPriority;
    using NS::Obj::VirtualCamera;

    constexpr float k_Dt = 1.0f / 60.0f;

    // 固定 pose を返すだけのテスト用 vcam。X 座標だけずらして補間が見えるようにする
    class FixedVcam : public VirtualCamera
    {
    public:
        explicit FixedVcam(float x) noexcept
            : VirtualCamera(TickPriority::LateUpdate + 50), m_x(x)
        {}
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
    void SetUp() override { NS::Platform::FrameTimer::SetFixedDelta(k_Dt); }
};

TEST_F(CameraBrainTest, PoseLerpInterpolatesEachField)
{
    CameraPose a{};
    a.position = NS::Core::Vector3{0.0f, 0.0f, 0.0f};
    a.fovY = NS::Core::Radians{1.0f};
    a.farPlane = 100.0f;
    CameraPose b{};
    b.position = NS::Core::Vector3{10.0f, 0.0f, 0.0f};
    b.fovY = NS::Core::Radians{3.0f};
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
    auto* brain = host.AddComponent<CameraBrain>();
    host.OnStart();
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
    auto* brain = host.AddComponent<CameraBrain>();
    host.OnStart();
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
    auto* brain = host.AddComponent<CameraBrain>();
    host.OnStart();
    brain->SetBlendDuration(0.5f);

    GameObject hostA;
    GameObject hostB;
    auto* a = hostA.AddComponent<FixedVcam>(0.0f);
    auto* b = hostB.AddComponent<FixedVcam>(10.0f);
    a->SetVcamPriority(20);
    b->SetVcamPriority(10);
    brain->AddVirtualCamera(a);
    brain->AddVirtualCamera(b);

    // 初回は旧 pose が無いのでカットで a に確定する
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(cam->Position().x, 0.0f);

    // b を優先に上げて切替 → ブレンド開始。1 ステップ目はまだ a 寄り
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
    auto* brain = host.AddComponent<CameraBrain>();
    host.OnStart();
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

TEST_F(CameraBrainTest, ActivatingHigherPriorityVcamBlendsTowardIt)
{
    GameObject host;
    auto* cam = host.AddComponent<CameraComponent>();
    auto* brain = host.AddComponent<CameraBrain>();
    host.OnStart();
    brain->SetBlendDuration(0.5f);

    GameObject followHost;
    GameObject areaHost;
    auto* follow = followHost.AddComponent<FixedVcam>(0.0f);
    auto* area = areaHost.AddComponent<FixedVcam>(10.0f);
    follow->SetVcamPriority(0);
    area->SetVcamPriority(10); // active な間だけ follow を上回る
    area->SetActive(false);
    brain->AddVirtualCamera(follow);
    brain->AddVirtualCamera(area);

    // area が休止の間は follow が選ばれる
    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_EQ(brain->ActiveVirtualCamera(), follow);
    EXPECT_FLOAT_EQ(cam->Position().x, 0.0f);

    // area を起こすとブレンドで近づく
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

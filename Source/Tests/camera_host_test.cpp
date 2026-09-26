#include <Runtime/Object/Component.h>
#include <Runtime/Object/Components/CameraBrain.h>
#include <Runtime/Object/Components/CameraComponent.h>
#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/Components/VirtualCamera.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <Runtime/Platform/Clock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
    using NS::Obj::Scene;

    // 追従カメラを 1 体だけ持つレベル。追う相手が無いので固定の既定視点を返す。組み立ては AssetManager 不在でも通る
    nlohmann::json MakeCameraLevel()
    {
        nlohmann::json data = NS::Obj::MakeSceneJson();
        nlohmann::json object = NS::Obj::MakeObjectJson();
        NS::Obj::ObjectJsonComponents(object).push_back(NS::Obj::MakeComponentEntry("ThirdPersonFollow"));
        NS::Obj::SceneJsonObjects(data).push_back(std::move(object));
        NS::Obj::EnsureUniqueObjectIds(data);
        return data;
    }

    NS::Obj::ThirdPersonFollow* FindCamera(Scene& scene)
    {
        NS::Obj::ThirdPersonFollow* found = nullptr;
        scene.Objects().ForEachComponent<NS::Obj::ThirdPersonFollow>([&found](NS::Obj::ThirdPersonFollow& follow) {
            if (found == nullptr)
                found = &follow;
        });
        return found;
    }
} // namespace

TEST(CameraHost, EverySceneHasCameraAndBrain)
{
    Scene scene;

    // どのシーンにも実カメラ + Brain が 1 組ある
    EXPECT_NE(scene.CameraBrain(), nullptr);
    EXPECT_NE(scene.MainCamera(), nullptr);
}

TEST(CameraHost, MainCameraLivesOnTheBrainObject)
{
    Scene scene;
    ASSERT_NE(scene.CameraBrain(), nullptr);

    // 2 つの読み口が同じ配置物の上の同じカメラを指す
    EXPECT_EQ(scene.MainCamera(), scene.CameraBrain()->Camera());
    EXPECT_EQ(scene.CameraBrain()->Owner(), scene.MainCamera()->Owner());
}

TEST(CameraHost, TeardownDropsTheHost)
{
    Scene scene;
    ASSERT_NE(scene.CameraBrain(), nullptr);

    // シーンを畳むと host ごと消え、読み口は nullptr を返す
    scene.OnShutdown();
    EXPECT_EQ(scene.CameraBrain(), nullptr);
    EXPECT_EQ(scene.MainCamera(), nullptr);
}

TEST(CameraHost, BrainRunsInTheLateUpdateBand)
{
    Scene scene;
    ASSERT_NE(scene.CameraBrain(), nullptr);
    // vcam を供給する追従カメラの LateUpdate + 50 より後ろに居る
    EXPECT_EQ(scene.CameraBrain()->Priority(), NS::Obj::TickPriority::LateUpdate + 60);

    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    ASSERT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), nullptr);

    // 帯が brain を回すので、進行から手で呼ぶ 1 行は要らない
    scene.OnUpdate();

    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), camera);
}

TEST(CameraHost, SurvivesRebuildAndRebindsVirtualCameras)
{
    Scene scene;
    NS::Obj::CameraBrain* brainBefore = scene.CameraBrain();
    ASSERT_NE(brainBefore, nullptr);
    const NS::Obj::GameObject* hostBefore = brainBefore->Owner();

    scene.LoadJson(MakeCameraLevel());
    // データから組み直しても同じ host が残る
    EXPECT_EQ(scene.CameraBrain(), brainBefore);
    EXPECT_EQ(scene.CameraBrain()->Owner(), hostBefore);

    NS::Obj::ThirdPersonFollow* first = FindCamera(scene);
    ASSERT_NE(first, nullptr);
    first->SetActive(true);
    scene.OnUpdate();
    ASSERT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), first);

    // 2 回目の組み直しで古い登録が外れ、新しい実体が選ばれる
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* second = FindCamera(scene);
    ASSERT_NE(second, nullptr);
    // アドレス比較はしない。解放直後の再確保が同じ番地を返すと、新しい実体でも偽で赤になる
    // 古い実体が残っていれば active のままここに出る。null は登録が外れて作り直された証拠
    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), nullptr);

    second->SetActive(true);
    scene.OnUpdate();
    EXPECT_EQ(scene.CameraBrain()->ActiveVirtualCamera(), second);
}

// 衝突の揺れ。vcam の pose は全部 Brain を通って実カメラへ書かれるので、どの vcam が選ばれていても一様に掛かる
// 縦のずれは追従カメラの上の向きに乗る
TEST(CameraHost, ShakeOffsetsFinalPose)
{
    Scene scene;
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    scene.OnUpdate();

    NS::Obj::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    brain->Evaluate(1.0f);
    const NS::Obj::CameraPose before = brain->LastPose();
    const NS::Core::Vector3 look = before.target - before.position;
    const NS::Core::Vector3 forward = brain->ForwardHorizontal();
    const NS::Core::Vector3 right{forward.z, 0.0f, -forward.x};
    NS::Core::Vector3 up = NS::Core::Cross(look, right);
    up.Normalize();

    ASSERT_TRUE(brain->StartShake(NS::Obj::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 4}));
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 moved = brain->LastPose().position - before.position;

    EXPECT_NEAR(NS::Core::Dot(moved, up), -0.1f, 1.0e-5f);
    EXPECT_NEAR(NS::Core::Dot(moved, right), 0.0f, 1.0e-5f);
}

// 揺れは position と target を同じだけ平行移動する。視線が回らないので camera 相対入力に波及しない
TEST(CameraHost, ShakeTranslatesViewWithoutTurning)
{
    Scene scene;
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    scene.OnUpdate();

    NS::Obj::CameraBrain* brain = scene.CameraBrain();
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 lookBefore = brain->LastPose().target - brain->LastPose().position;
    const NS::Core::Vector3 forwardBefore = brain->ForwardHorizontal();

    ASSERT_TRUE(brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.1f,
        .upAmplitude = 0.05f,
        .frames = 4,
        .longestFlipFrames = 2,
        .seed = 5,
    }));
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 lookDuring = brain->LastPose().target - brain->LastPose().position;
    const NS::Core::Vector3 forwardDuring = brain->ForwardHorizontal();

    EXPECT_FLOAT_EQ(lookDuring.x, lookBefore.x);
    EXPECT_FLOAT_EQ(lookDuring.y, lookBefore.y);
    EXPECT_FLOAT_EQ(lookDuring.z, lookBefore.z);
    EXPECT_FLOAT_EQ(forwardDuring.x, forwardBefore.x);
    EXPECT_FLOAT_EQ(forwardDuring.z, forwardBefore.z);
}

// 揺れは始めたフレームを含めて渡したフレーム数の中で減衰し切り、その後に尾を引かない
TEST(CameraHost, ShakeEndsWithinSteps)
{
    Scene scene;
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    scene.OnUpdate();

    NS::Obj::CameraBrain* brain = scene.CameraBrain();
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 before = brain->LastPose().position;

    ASSERT_TRUE(brain->StartShake(NS::Obj::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 3}));
    for (int frame = 0; frame < 3; ++frame)
    {
        brain->OnUpdate();
        brain->Evaluate(1.0f);
        EXPECT_GT(std::abs(brain->LastPose().position.y - before.y), 1.0e-4f) << "frame " << frame;
    }

    brain->OnUpdate();
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);
}

// 壊れた値は受け取らない。負の振れ幅と 0 フレームは揺れを始めない
TEST(CameraHost, ShakeRejectsBrokenInput)
{
    Scene scene;
    scene.LoadJson(MakeCameraLevel());
    NS::Obj::ThirdPersonFollow* camera = FindCamera(scene);
    ASSERT_NE(camera, nullptr);
    camera->SetActive(true);
    scene.OnUpdate();

    NS::Obj::CameraBrain* brain = scene.CameraBrain();
    brain->Evaluate(1.0f);
    const NS::Core::Vector3 before = brain->LastPose().position;

    EXPECT_FALSE(brain->StartShake(NS::Obj::CameraShakeDesc{.upAmplitude = -1.0f, .frames = 4}));
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);

    EXPECT_FALSE(brain->StartShake(NS::Obj::CameraShakeDesc{.upAmplitude = 0.1f, .frames = 0}));
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);
}

namespace
{
    constexpr float k_OffsetTolerance = 1.0e-5f;
    constexpr std::uint32_t k_FlipSeedCount = 16;
    constexpr int k_FlipFrames = 40;

    // 見下ろして斜めを向く固定の視点。右と上がどの世界の軸とも揃わないので、ずれの軸を見分けられる
    class TiltedVcam : public NS::Obj::VirtualCamera
    {
    public:
        TiltedVcam() noexcept : VirtualCamera(NS::Obj::TickPriority::LateUpdate + 50) {}

        [[nodiscard]] NS::Obj::CameraPose EvaluatePose(float) const noexcept override
        {
            return MakePose(Position(), Target(), NS::Core::Vector3{0.0f, 1.0f, 0.0f});
        }

        [[nodiscard]] static NS::Core::Vector3 Target() noexcept { return NS::Core::Vector3{1.0f, 2.0f, 3.0f}; }

        // ヨー 30 度、ピッチ -15 度の視線
        [[nodiscard]] static NS::Core::Vector3 Look() noexcept
        {
            const float yaw = NS::Core::DegreesToRadians(30.0f);
            const float pitch = NS::Core::DegreesToRadians(-15.0f);
            return NS::Core::Vector3{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        }

        [[nodiscard]] static NS::Core::Vector3 Position() noexcept { return Target() - Look() * 5.0f; }

        [[nodiscard]] static NS::Core::Vector3 Right() noexcept
        {
            const NS::Core::Vector3 look = Look();
            const float horizontal = std::sqrt(look.x * look.x + look.z * look.z);
            return NS::Core::Vector3{look.z / horizontal, 0.0f, -look.x / horizontal};
        }

        [[nodiscard]] static NS::Core::Vector3 Up() noexcept
        {
            NS::Core::Vector3 up = NS::Core::Cross(Look(), Right());
            up.Normalize();
            return up;
        }
    };

    // 実カメラと Brain を載せた配置物に、斜めの視点を 1 つだけ登録した台。ブレンドは切ってある
    struct ShapedShakeRig
    {
        NS::Obj::GameObject host;
        NS::Obj::GameObject vcamHost;
        NS::Obj::CameraComponent* camera = nullptr;
        NS::Obj::CameraBrain* brain = nullptr;

        ShapedShakeRig()
        {
            NS::Platform::FrameTimer::SetFixedDelta(1.0f / 60.0f);
            camera = host.AddComponent<NS::Obj::CameraComponent>();
            brain = host.AddComponent<NS::Obj::CameraBrain>();
            host.OnStart();
            brain->SetBlendDuration(0.0f);
            brain->AddVirtualCamera(vcamHost.AddComponent<TiltedVcam>());
            brain->OnUpdate();
            brain->Evaluate(1.0f);
        }

        // ゲームの帯と同じ順で 1 フレーム進めて描く
        void Step() const
        {
            brain->OnUpdate();
            brain->Evaluate(1.0f);
        }

        // 描いた位置から揺れの無い位置を引き、カメラの右と上の成分へ分ける
        [[nodiscard]] NS::Core::Vector2 DrawnOffset() const
        {
            const NS::Core::Vector3 moved = brain->LastPose().position - TiltedVcam::Position();
            return NS::Core::Vector2{NS::Core::Dot(moved, TiltedVcam::Right()), NS::Core::Dot(moved, TiltedVcam::Up())};
        }

        // 描いた上の向きが、揺れの無い上からカメラの右へ倒れた角度 (度)
        [[nodiscard]] float DrawnRollDegrees() const
        {
            const NS::Core::Vector3 up = brain->LastPose().up;
            return NS::Core::RadiansToDegrees(
                std::atan2(NS::Core::Dot(up, TiltedVcam::Right()), NS::Core::Dot(up, TiltedVcam::Up())));
        }
    };

    NS::Obj::CameraShakeDesc FlipShake(std::uint32_t seed)
    {
        return NS::Obj::CameraShakeDesc{
            .sideAmplitude = 0.05f,
            .upAmplitude = 0.02f,
            .frames = k_FlipFrames,
            .longestFlipFrames = 3,
            .firstSideDirection = TiltedVcam::Right(),
            .seed = seed,
        };
    }

    int SignOf(float value)
    {
        if (value > 0.0f)
        {
            return 1;
        }
        if (value < 0.0f)
        {
            return -1;
        }
        return 0;
    }

    // 横と縦それぞれのフレームごとの向きの符号
    struct FlipSigns
    {
        std::vector<int> side;
        std::vector<int> up;
    };

    FlipSigns RecordFlipSigns(std::uint32_t seed)
    {
        ShapedShakeRig rig;
        FlipSigns signs;
        EXPECT_TRUE(rig.brain->StartShake(FlipShake(seed)));
        for (int frame = 0; frame < k_FlipFrames; ++frame)
        {
            rig.Step();
            signs.side.push_back(SignOf(rig.brain->ShakeOffset().x));
            signs.up.push_back(SignOf(rig.brain->ShakeOffset().y));
        }
        return signs;
    }

    // 同じ符号が続いたフレーム数の並び。揺れの終わりで切れた最後の並びは入れない
    std::vector<int> CompleteRunLengths(const std::vector<int>& signs)
    {
        std::vector<int> runs;
        int length = 0;
        for (std::size_t frame = 0; frame < signs.size(); ++frame)
        {
            ++length;
            const bool flipsNext = frame + 1 < signs.size() && signs[frame + 1] != signs[frame];
            if (flipsNext)
            {
                runs.push_back(length);
                length = 0;
            }
        }
        return runs;
    }

    std::vector<int> FlipFrames(const std::vector<int>& signs)
    {
        std::vector<int> frames;
        for (std::size_t frame = 1; frame < signs.size(); ++frame)
        {
            if (signs[frame] != signs[frame - 1])
            {
                frames.push_back(static_cast<int>(frame));
            }
        }
        return frames;
    }

    // カメラの右の側、左の側、どちらでもない向きと、それぞれで期待する符号
    std::vector<std::pair<NS::Core::Vector3, int>> SideDirections()
    {
        NS::Core::Vector3 forward = TiltedVcam::Look();
        forward.y = 0.0f;
        forward.Normalize();
        return {
            {TiltedVcam::Right() + forward * 5.0f, 1},
            {TiltedVcam::Right() * -1.0f + forward * 5.0f, -1},
            {NS::Core::Vector3{0.0f, 1.0f, 0.0f}, 1},
        };
    }
} // namespace

// 揺れを始めたフレームは、同じフレームの OnUpdate を回しても最初の振れを最大で描く
TEST(CameraHost, ShapedShakeDrawsTheFirstSwingOnTheStartFrame)
{
    ShapedShakeRig rig;
    ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.08f,
        .upAmplitude = 0.03f,
        .frames = 6,
        .longestFlipFrames = 3,
        .firstSideDirection = TiltedVcam::Right(),
        .seed = 7,
    }));

    rig.brain->Evaluate(1.0f);
    const NS::Core::Vector2 beforeUpdate = rig.DrawnOffset();
    rig.Step();
    const NS::Core::Vector2 afterUpdate = rig.DrawnOffset();

    EXPECT_NEAR(beforeUpdate.x, 0.08f, k_OffsetTolerance);
    EXPECT_NEAR(beforeUpdate.y, -0.03f, k_OffsetTolerance);
    EXPECT_NEAR(afterUpdate.x, 0.08f, k_OffsetTolerance);
    EXPECT_NEAR(afterUpdate.y, -0.03f, k_OffsetTolerance);
}

// 渡したフレーム数を、始めたフレームを含めて描き、その後は 0
TEST(CameraHost, ShapedShakeDrawsForItsFrameCountIncludingTheStartFrame)
{
    ShapedShakeRig rig;
    ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.08f,
        .upAmplitude = 0.03f,
        .frames = 5,
        .longestFlipFrames = 3,
        .firstSideDirection = TiltedVcam::Right(),
        .seed = 7,
    }));

    int drawnFrames = 0;
    for (int frame = 0; frame < 8; ++frame)
    {
        rig.Step();
        if (rig.DrawnOffset().Length() > k_OffsetTolerance)
        {
            ++drawnFrames;
        }
    }

    EXPECT_EQ(drawnFrames, 5);
    EXPECT_EQ(rig.brain->LastPose().position, TiltedVcam::Position());
}

// ずれは見下ろすカメラの右と上に乗り、位置と注視点を同じだけ動かす
TEST(CameraHost, ShapedShakeMovesAlongTheCameraRightAndUp)
{
    ShapedShakeRig rig;
    ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.08f,
        .upAmplitude = 0.03f,
        .frames = 4,
        .longestFlipFrames = 1,
        .firstSideDirection = TiltedVcam::Right(),
        .seed = 0,
    }));
    rig.Step();

    const NS::Core::Vector3 expected = TiltedVcam::Right() * 0.08f - TiltedVcam::Up() * 0.03f;
    const NS::Core::Vector3 moved = rig.brain->LastPose().position - TiltedVcam::Position();
    const NS::Core::Vector3 targetMoved = rig.brain->LastPose().target - TiltedVcam::Target();
    EXPECT_NEAR(moved.x, expected.x, k_OffsetTolerance);
    EXPECT_NEAR(moved.y, expected.y, k_OffsetTolerance);
    EXPECT_NEAR(moved.z, expected.z, k_OffsetTolerance);
    EXPECT_NEAR(targetMoved.x, expected.x, k_OffsetTolerance);
    EXPECT_NEAR(targetMoved.y, expected.y, k_OffsetTolerance);
    EXPECT_NEAR(targetMoved.z, expected.z, k_OffsetTolerance);
}

// ずれの大きさは直線に減り、前のフレームより大きくならない
TEST(CameraHost, ShapedShakeFadesLinearly)
{
    ShapedShakeRig rig;
    constexpr int frames = 8;
    ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.08f,
        .upAmplitude = 0.03f,
        .frames = frames,
        .longestFlipFrames = 3,
        .firstSideDirection = TiltedVcam::Right(),
        .seed = 3,
    }));

    const float first = NS::Core::Vector2{0.08f, 0.03f}.Length();
    float previous = first;
    for (int frame = 0; frame < frames; ++frame)
    {
        rig.Step();
        const float length = rig.DrawnOffset().Length();
        EXPECT_NEAR(length, first * static_cast<float>(frames - frame) / static_cast<float>(frames), k_OffsetTolerance)
            << "frame " << frame;
        EXPECT_LE(length, previous + k_OffsetTolerance) << "frame " << frame;
        previous = length;
    }
}

// 横の振れ幅 0・最長 1 は、縦だけが毎フレーム入れ替わり、最初は下
TEST(CameraHost, ShapedShakeWithoutSideFlipsUpEveryFrameStartingDown)
{
    ShapedShakeRig rig;
    constexpr int frames = 6;
    ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.0f,
        .upAmplitude = 0.05f,
        .frames = frames,
        .longestFlipFrames = 1,
        .firstSideDirection = TiltedVcam::Right(),
        .seed = 11,
    }));

    float sign = -1.0f;
    for (int frame = 0; frame < frames; ++frame)
    {
        rig.Step();
        const NS::Core::Vector2 offset = rig.DrawnOffset();
        EXPECT_NEAR(offset.x, 0.0f, k_OffsetTolerance) << "frame " << frame;
        EXPECT_NEAR(
            offset.y, sign * 0.05f * static_cast<float>(frames - frame) / static_cast<float>(frames), k_OffsetTolerance)
            << "frame " << frame;
        sign = -sign;
    }
}

// 最長 1 は横も毎フレーム入れ替わる。続けて同じ間隔を選ばない決まりは掛からない
TEST(CameraHost, ShapedShakeWithLongestOneFlipsSideEveryFrame)
{
    ShapedShakeRig rig;
    constexpr int frames = 6;
    ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.05f,
        .upAmplitude = 0.02f,
        .frames = frames,
        .longestFlipFrames = 1,
        .firstSideDirection = TiltedVcam::Right(),
        .seed = 11,
    }));

    int sideSign = 1;
    int upSign = -1;
    for (int frame = 0; frame < frames; ++frame)
    {
        rig.Step();
        EXPECT_EQ(SignOf(rig.DrawnOffset().x), sideSign) << "frame " << frame;
        EXPECT_EQ(SignOf(rig.DrawnOffset().y), upSign) << "frame " << frame;
        sideSign = -sideSign;
        upSign = -upSign;
    }
}

// 最長 2 以上では、横と縦それぞれの向きが 1〜最長フレームごとに入れ替わり、続けて同じ間隔にならない
// 横と縦は別の並びで、同じ種なら同じ並び、違う種なら違う並び
TEST(CameraHost, ShapedShakeFlipIntervalsVaryWithinTheLongest)
{
    for (std::uint32_t seed = 1; seed <= k_FlipSeedCount; ++seed)
    {
        const FlipSigns signs = RecordFlipSigns(seed);
        const std::vector<int> sideRuns = CompleteRunLengths(signs.side);
        const std::vector<int> upRuns = CompleteRunLengths(signs.up);
        for (const std::vector<int>& runs : {sideRuns, upRuns})
        {
            ASSERT_GE(runs.size(), 3u) << "seed " << seed;
            for (std::size_t run = 0; run < runs.size(); ++run)
            {
                EXPECT_GE(runs[run], 1) << "seed " << seed;
                EXPECT_LE(runs[run], 3) << "seed " << seed;
                if (run > 0)
                {
                    EXPECT_NE(runs[run], runs[run - 1]) << "seed " << seed << " run " << run;
                }
            }
        }
        EXPECT_NE(FlipFrames(signs.side), FlipFrames(signs.up)) << "seed " << seed;

        const FlipSigns again = RecordFlipSigns(seed);
        EXPECT_EQ(again.side, signs.side) << "seed " << seed;
        EXPECT_EQ(again.up, signs.up) << "seed " << seed;

        const FlipSigns other = RecordFlipSigns(seed + k_FlipSeedCount);
        const bool differs = other.side != signs.side || other.up != signs.up;
        EXPECT_TRUE(differs) << "seed " << seed;
    }
}

// 最初の横の振れは、渡した世界の向きがカメラの右の側なら右、左の側なら左、どちらでもなければ右
TEST(CameraHost, ShapedShakeFirstSideFollowsTheGivenDirection)
{
    for (const std::pair<NS::Core::Vector3, int>& direction : SideDirections())
    {
        ShapedShakeRig rig;
        ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
            .sideAmplitude = 0.05f,
            .upAmplitude = 0.02f,
            .frames = 4,
            .longestFlipFrames = 3,
            .firstSideDirection = direction.first,
            .seed = 5,
        }));
        rig.Step();
        EXPECT_EQ(SignOf(rig.DrawnOffset().x), direction.second);
    }
}

// 読み口は Evaluate が足したずれと同じ値を返す
TEST(CameraHost, ShakeOffsetMatchesWhatEvaluateAdds)
{
    ShapedShakeRig rig;
    ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.05f,
        .upAmplitude = 0.02f,
        .frames = 10,
        .longestFlipFrames = 3,
        .firstSideDirection = TiltedVcam::Right(),
        .seed = 5,
    }));

    for (int frame = 0; frame < 11; ++frame)
    {
        rig.Step();
        EXPECT_NEAR(rig.brain->ShakeOffset().x, rig.DrawnOffset().x, k_OffsetTolerance) << "frame " << frame;
        EXPECT_NEAR(rig.brain->ShakeOffset().y, rig.DrawnOffset().y, k_OffsetTolerance) << "frame " << frame;
    }
}

// 揺れと寄りと傾きの間も視線の向きは回らない。カメラを基準にした入力の向きは変わらない
TEST(CameraHost, ShakeAndZoomRollKeepTheViewDirection)
{
    ShapedShakeRig rig;
    const NS::Core::Vector3 lookBefore = rig.brain->LastPose().target - rig.brain->LastPose().position;
    const NS::Core::Vector3 forwardBefore = rig.brain->ForwardHorizontal();

    ASSERT_TRUE(rig.brain->StartShake(FlipShake(9)));
    ASSERT_TRUE(rig.brain->StartZoomRoll(NS::Obj::CameraZoomRollDesc{
        .zoom = 1.2f,
        .rollDegrees = 4.0f,
        .rollDirection = TiltedVcam::Right(),
        .holdFrames = 2,
        .returnFrames = 3,
    }));

    for (int frame = 0; frame < 6; ++frame)
    {
        rig.Step();
        const NS::Core::Vector3 look = rig.brain->LastPose().target - rig.brain->LastPose().position;
        EXPECT_NEAR(look.x, lookBefore.x, k_OffsetTolerance) << "frame " << frame;
        EXPECT_NEAR(look.y, lookBefore.y, k_OffsetTolerance) << "frame " << frame;
        EXPECT_NEAR(look.z, lookBefore.z, k_OffsetTolerance) << "frame " << frame;
        EXPECT_NEAR(rig.brain->ForwardHorizontal().x, forwardBefore.x, k_OffsetTolerance) << "frame " << frame;
        EXPECT_NEAR(rig.brain->ForwardHorizontal().z, forwardBefore.z, k_OffsetTolerance) << "frame " << frame;
    }
}

// 寄りと傾きは始めたフレームから全部入り、保つ間そのまま、戻すフレーム数で滑らかに戻り、最後のフレームでちょうど元に戻る
TEST(CameraHost, ZoomRollHoldsThenEasesBack)
{
    ShapedShakeRig rig;
    const float baseFov = rig.brain->LastPose().fovY.value;
    constexpr float zoom = 1.2f;
    constexpr float roll = 4.0f;
    constexpr int hold = 3;
    constexpr int back = 4;
    ASSERT_TRUE(rig.brain->StartZoomRoll(NS::Obj::CameraZoomRollDesc{
        .zoom = zoom,
        .rollDegrees = roll,
        .rollDirection = TiltedVcam::Right(),
        .holdFrames = hold,
        .returnFrames = back,
    }));

    for (int frame = 0; frame < hold + back; ++frame)
    {
        rig.Step();
        float weight = 0.0f;
        if (frame >= hold)
        {
            const float t = static_cast<float>(frame - hold + 1) / static_cast<float>(back);
            weight = t * t * (3.0f - 2.0f * t);
        }
        EXPECT_NEAR(rig.brain->ZoomRoll().zoom, zoom * (1.0f - weight) + weight, 1.0e-6f) << "frame " << frame;
        EXPECT_NEAR(rig.brain->ZoomRoll().rollDegrees, roll * (1.0f - weight), 1.0e-5f) << "frame " << frame;

        const float expectedFov = 2.0f * std::atan(std::tan(baseFov * 0.5f) / rig.brain->ZoomRoll().zoom);
        EXPECT_NEAR(rig.brain->LastPose().fovY.value, expectedFov, 1.0e-6f) << "frame " << frame;
        EXPECT_NEAR(rig.DrawnRollDegrees(), rig.brain->ZoomRoll().rollDegrees, 1.0e-3f) << "frame " << frame;
        // 傾けた上は視線と直交する。傾き 0 の時は仮想カメラの上のまま
        if (rig.brain->ZoomRoll().rollDegrees != 0.0f)
        {
            EXPECT_NEAR(NS::Core::Dot(rig.brain->LastPose().up, TiltedVcam::Look()), 0.0f, 1.0e-5f)
                << "frame " << frame;
        }
    }

    EXPECT_EQ(rig.brain->ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(rig.brain->ZoomRoll().rollDegrees, 0.0f);
    EXPECT_EQ(rig.brain->LastPose().fovY.value, baseFov);

    rig.Step();
    EXPECT_EQ(rig.brain->ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(rig.brain->ZoomRoll().rollDegrees, 0.0f);
}

// 傾きの向きは、渡した世界の向きがカメラの右の側なら上端を右へ、左の側なら左へ、どちらでもなければ右へ倒す
TEST(CameraHost, ZoomRollTiltsTowardTheGivenDirection)
{
    for (const std::pair<NS::Core::Vector3, int>& direction : SideDirections())
    {
        ShapedShakeRig rig;
        ASSERT_TRUE(rig.brain->StartZoomRoll(NS::Obj::CameraZoomRollDesc{
            .zoom = 1.1f,
            .rollDegrees = 3.0f,
            .rollDirection = direction.first,
            .holdFrames = 2,
            .returnFrames = 2,
        }));
        rig.Step();
        EXPECT_EQ(SignOf(rig.brain->ZoomRoll().rollDegrees), direction.second);
        EXPECT_EQ(SignOf(rig.DrawnRollDegrees()), direction.second);
    }
}

// 揺れの途中の StartShake は新しい最初の振れから、戻しの途中の StartZoomRoll は新しい倍率から始め直す
TEST(CameraHost, RestartingOverridesTheRunningShakeAndZoomRoll)
{
    ShapedShakeRig rig;
    ASSERT_TRUE(rig.brain->StartShake(FlipShake(4)));
    ASSERT_TRUE(rig.brain->StartZoomRoll(NS::Obj::CameraZoomRollDesc{
        .zoom = 1.3f,
        .rollDegrees = 5.0f,
        .rollDirection = TiltedVcam::Right(),
        .holdFrames = 1,
        .returnFrames = 6,
    }));
    for (int frame = 0; frame < 3; ++frame)
    {
        rig.Step();
    }

    ASSERT_TRUE(rig.brain->StartShake(NS::Obj::CameraShakeDesc{
        .sideAmplitude = 0.02f,
        .upAmplitude = 0.01f,
        .frames = 4,
        .longestFlipFrames = 3,
        .firstSideDirection = TiltedVcam::Right(),
        .seed = 8,
    }));
    ASSERT_TRUE(rig.brain->StartZoomRoll(NS::Obj::CameraZoomRollDesc{
        .zoom = 1.1f,
        .rollDegrees = 2.0f,
        .rollDirection = TiltedVcam::Right(),
        .holdFrames = 2,
        .returnFrames = 2,
    }));
    rig.Step();

    EXPECT_NEAR(rig.DrawnOffset().x, 0.02f, k_OffsetTolerance);
    EXPECT_NEAR(rig.DrawnOffset().y, -0.01f, k_OffsetTolerance);
    EXPECT_FLOAT_EQ(rig.brain->ZoomRoll().zoom, 1.1f);
    EXPECT_FLOAT_EQ(rig.brain->ZoomRoll().rollDegrees, 2.0f);
}

// 止めた後はずれ 0・倍率 1・傾き 0 で描く
TEST(CameraHost, StopShakeAndZoomRollClearsEverything)
{
    ShapedShakeRig rig;
    const float baseFov = rig.brain->LastPose().fovY.value;
    ASSERT_TRUE(rig.brain->StartShake(FlipShake(2)));
    ASSERT_TRUE(rig.brain->StartZoomRoll(NS::Obj::CameraZoomRollDesc{
        .zoom = 1.2f,
        .rollDegrees = 3.0f,
        .rollDirection = TiltedVcam::Right(),
        .holdFrames = 5,
        .returnFrames = 5,
    }));
    rig.Step();

    rig.brain->StopShakeAndZoomRoll();
    rig.brain->Evaluate(1.0f);

    EXPECT_EQ(rig.brain->ShakeOffset().x, 0.0f);
    EXPECT_EQ(rig.brain->ShakeOffset().y, 0.0f);
    EXPECT_EQ(rig.brain->ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(rig.brain->ZoomRoll().rollDegrees, 0.0f);
    EXPECT_EQ(rig.brain->LastPose().position, TiltedVcam::Position());
    EXPECT_EQ(rig.brain->LastPose().fovY.value, baseFov);
    EXPECT_EQ(rig.brain->LastPose().up, (NS::Core::Vector3{0.0f, 1.0f, 0.0f}));
}

// 合成を返す関数は Evaluate が書く姿勢と同じ姿勢を返し、実カメラへは書かない
TEST(CameraHost, ComposePoseMatchesTheDrawnPoseWithoutWriting)
{
    ShapedShakeRig rig;
    ASSERT_TRUE(rig.brain->StartShake(FlipShake(6)));
    ASSERT_TRUE(rig.brain->StartZoomRoll(NS::Obj::CameraZoomRollDesc{
        .zoom = 1.2f,
        .rollDegrees = 3.0f,
        .rollDirection = TiltedVcam::Right(),
        .holdFrames = 1,
        .returnFrames = 4,
    }));
    rig.Step();

    const std::optional<NS::Obj::CameraPose> composed = rig.brain->ComposePose(1.0f);
    ASSERT_TRUE(composed.has_value());
    const NS::Obj::CameraPose drawn = rig.brain->LastPose();
    EXPECT_EQ(composed->position, drawn.position);
    EXPECT_EQ(composed->target, drawn.target);
    EXPECT_EQ(composed->up, drawn.up);
    EXPECT_EQ(composed->fovY.value, drawn.fovY.value);

    // 次のフレームの合成は今の姿勢と違うが、呼んでも実カメラは今の姿勢のまま
    rig.brain->OnUpdate();
    const std::optional<NS::Obj::CameraPose> next = rig.brain->ComposePose(1.0f);
    ASSERT_TRUE(next.has_value());
    EXPECT_NE(next->position, drawn.position);
    EXPECT_EQ(rig.camera->Position(), drawn.position);
    EXPECT_EQ(rig.camera->FovY().value, drawn.fovY.value);
}

// 壊れた設定は偽を返し、動いている揺れと寄りと傾きを変えない
TEST(CameraHost, ShapedShakeAndZoomRollRejectBrokenDesc)
{
    ShapedShakeRig rig;
    ASSERT_TRUE(rig.brain->StartShake(FlipShake(10)));
    ASSERT_TRUE(rig.brain->StartZoomRoll(NS::Obj::CameraZoomRollDesc{
        .zoom = 1.2f,
        .rollDegrees = 3.0f,
        .rollDirection = TiltedVcam::Right(),
        .holdFrames = 5,
        .returnFrames = 5,
    }));
    rig.Step();
    const NS::Core::Vector2 offset = rig.brain->ShakeOffset();
    const NS::Obj::CameraZoomRoll zoomRoll = rig.brain->ZoomRoll();

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<NS::Obj::CameraShakeDesc> brokenShakes(10, FlipShake(20));
    brokenShakes[0].sideAmplitude = nan;
    brokenShakes[1].upAmplitude = nan;
    brokenShakes[2].sideAmplitude = infinity;
    brokenShakes[3].upAmplitude = infinity;
    brokenShakes[4].sideAmplitude = -0.01f;
    brokenShakes[5].upAmplitude = -0.01f;
    brokenShakes[6].frames = 0;
    brokenShakes[7].frames = -1;
    brokenShakes[8].longestFlipFrames = 0;
    brokenShakes[9].firstSideDirection = NS::Core::Vector3{nan, 0.0f, 0.0f};
    for (std::size_t index = 0; index < brokenShakes.size(); ++index)
    {
        EXPECT_FALSE(rig.brain->StartShake(brokenShakes[index])) << "shake " << index;
    }

    const NS::Obj::CameraZoomRollDesc validZoomRoll{
        .zoom = 1.1f,
        .rollDegrees = 2.0f,
        .rollDirection = TiltedVcam::Right(),
        .holdFrames = 2,
        .returnFrames = 2,
    };
    std::vector<NS::Obj::CameraZoomRollDesc> brokenZoomRolls(7, validZoomRoll);
    brokenZoomRolls[0].zoom = 0.9f;
    brokenZoomRolls[1].zoom = nan;
    brokenZoomRolls[2].zoom = infinity;
    brokenZoomRolls[3].rollDegrees = nan;
    brokenZoomRolls[4].rollDirection = NS::Core::Vector3{0.0f, nan, 0.0f};
    brokenZoomRolls[5].holdFrames = -1;
    brokenZoomRolls[6].returnFrames = -1;
    for (std::size_t index = 0; index < brokenZoomRolls.size(); ++index)
    {
        EXPECT_FALSE(rig.brain->StartZoomRoll(brokenZoomRolls[index])) << "zoom roll " << index;
    }

    rig.brain->Evaluate(1.0f);
    EXPECT_EQ(rig.brain->ShakeOffset().x, offset.x);
    EXPECT_EQ(rig.brain->ShakeOffset().y, offset.y);
    EXPECT_EQ(rig.brain->ZoomRoll().zoom, zoomRoll.zoom);
    EXPECT_EQ(rig.brain->ZoomRoll().rollDegrees, zoomRoll.rollDegrees);
}

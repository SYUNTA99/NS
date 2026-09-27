#include "Editor/EditorObjects.h"
#include "Game/Player.h"

#include <Game/Level/Breakable.h>
#include <Game/Level/CollisionInput.h>
#include <Game/Level/ImpactResolver.h>
#include <Game/Level/TargetMarker.h>
#include <Game/Player/PlayerComponent.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Core/CameraData.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectJson.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/ObjectRef.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Object/Scene/SceneJson.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Mouse.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Obj;

// 形を組む関数は溜め量・威力・質量を受け取らない。受け取る口が無ければ、それらで形を変えられない
static_assert(std::is_same_v<decltype(&LevelNs::BuildTargetMarkerShape),
                             bool (*)(const NS::Core::Matrix&,
                                      NS::Core::Size2D,
                                      const LevelNs::SlamLineTarget&,
                                      float,
                                      const LevelNs::TargetMarkerDesc&,
                                      LevelNs::TargetMarkerShape&)>);
static_assert(std::is_same_v<decltype(&LevelNs::BuildAimPathShape),
                             bool (*)(const NS::Core::Matrix&,
                                      NS::Core::Size2D,
                                      const LevelNs::AimLine&,
                                      float,
                                      const LevelNs::TargetMarkerDesc&,
                                      LevelNs::TargetMarkerShape&)>);

namespace
{
    using NS::Core::Vector3;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr NS::Core::Size2D k_TargetSize{1280, 720};
    // 同梱シーンの自機の玉の半径
    constexpr float k_BallRadius = 0.65f;
    constexpr float k_Tolerance = 1e-3f;

    [[nodiscard]] NS::Core::Matrix LookFrom(const Vector3& position, const Vector3& target)
    {
        NS::Core::CameraData camera;
        camera.SetPosition(position);
        camera.SetTarget(target);
        return camera.ViewProjection();
    }

    // 試しの側の物差し。世界の点を描画先の画素 (左上が原点) へ投げる
    [[nodiscard]] NS::Core::Vector2 ToPixels(const NS::Core::Matrix& viewProjection, const Vector3& point)
    {
        const NS::Core::Vector4 clip =
            NS::Core::Vector4::Transform(NS::Core::Vector4{point.x, point.y, point.z, 1.0f}, viewProjection);
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        return NS::Core::Vector2{(ndcX + 1.0f) * 0.5f * static_cast<float>(k_TargetSize.width),
                                 (1.0f - ndcY) * 0.5f * static_cast<float>(k_TargetSize.height)};
    }

    // +X の線の上、自機の玉の中心 (0, 1, 0) から 5 m 先の 1 m 立方の相手
    [[nodiscard]] LevelNs::SlamLineTarget LineTargetAhead(float lateral, float offset)
    {
        return LevelNs::SlamLineTarget{.target = SceneNs::ObjectRef{7},
                                       .bounds =
                                           NS::Core::AABB{Vector3{5.0f, 1.0f, lateral}, Vector3{0.5f, 0.5f, 0.5f}},
                                       .origin = Vector3{0.0f, 1.0f, 0.0f},
                                       .direction = Vector3{1.0f, 0.0f, 0.0f},
                                       .along = 5.0f,
                                       .offset = offset};
    }

    void ExpectRectNear(const LevelNs::MarkerRect& actual, const LevelNs::MarkerRect& expected, std::size_t index)
    {
        EXPECT_NEAR(actual.x, expected.x, k_Tolerance) << index;
        EXPECT_NEAR(actual.y, expected.y, k_Tolerance) << index;
        EXPECT_NEAR(actual.width, expected.width, k_Tolerance) << index;
        EXPECT_NEAR(actual.height, expected.height, k_Tolerance) << index;
    }

    void ExpectShapeNear(const LevelNs::TargetMarkerShape& actual, const LevelNs::TargetMarkerShape& expected)
    {
        ASSERT_EQ(actual.corners.size(), expected.corners.size());
        ASSERT_EQ(actual.dots.size(), expected.dots.size());
        for (std::size_t i = 0; i < expected.corners.size(); ++i)
        {
            ExpectRectNear(actual.corners[i], expected.corners[i], i);
        }
        for (std::size_t i = 0; i < expected.dots.size(); ++i)
        {
            ExpectRectNear(actual.dots[i], expected.dots[i], i);
        }
    }

    [[nodiscard]] NS::Core::Vector2 RectCenter(const LevelNs::MarkerRect& rect) noexcept
    {
        return NS::Core::Vector2{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
    }

    // 押しの入口は CollisionInput が読む実機のマウスなので、試しの後に押したまま残すと他の試しが押しを拾う
    struct MouseLeftPress
    {
        MouseLeftPress() noexcept { NS::Platform::Input::Get().Mouse().OnButtonDown(NS::Platform::MouseButton::Left); }
        ~MouseLeftPress() noexcept { NS::Platform::Input::Get().Mouse().ClearState(); }
        MouseLeftPress(const MouseLeftPress&) = delete;
        MouseLeftPress& operator=(const MouseLeftPress&) = delete;

        void Release() noexcept { NS::Platform::Input::Get().Mouse().OnButtonUp(NS::Platform::MouseButton::Left); }
    };

    struct MarkerCourse
    {
        bool withCollisionInput = true;
        float targetMass = 1.0f;
    };

    struct MarkerRig
    {
        NS::Game::Player::PlayerComponent* movement = nullptr;
        LevelNs::CollisionInput* input = nullptr;
        LevelNs::TargetMarker* marker = nullptr;
        SceneNs::GameObject* target = nullptr;
    };

    // 自機を原点に置き、+X の 5 m 先の床の上に壊せる的を 1 体置く
    MarkerRig BuildMarkerCourse(SceneNs::Scene& scene, const MarkerCourse& course)
    {
        NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);

        nlohmann::json data = SceneNs::MakeSceneJson();
        nlohmann::json player = MakePlayerObject(Vector3{0.0f, 1.41f, 0.0f}, NS::Core::Quaternion{});
        SceneNs::ObjectJsonComponents(player).push_back(SceneNs::MakeComponentEntry("ImpactResolver"));
        if (course.withCollisionInput)
        {
            SceneNs::ObjectJsonComponents(player).push_back(SceneNs::MakeComponentEntry("CollisionInput"));
        }
        SceneNs::ObjectJsonComponents(player).push_back(SceneNs::MakeComponentEntry("TargetMarker"));
        SceneNs::SceneJsonObjects(data).push_back(player);

        for (std::int16_t i = -3; i <= 8; ++i)
        {
            SceneNs::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(i, 0, 0));
        }

        nlohmann::json target = NS::Editor::MakeCellObject(5, 1, 0);
        nlohmann::json rigidBody = SceneNs::MakeComponentEntry("RigidBody");
        SceneNs::SetField(rigidBody, "キネマティック", true);
        SceneNs::SetField(rigidBody, "質量", course.targetMass);
        SceneNs::ObjectJsonComponents(target).push_back(rigidBody);
        SceneNs::ObjectJsonComponents(target).push_back(SceneNs::MakeComponentEntry("Breakable"));
        SceneNs::SceneJsonObjects(data).push_back(target);
        scene.LoadJson(std::move(data));

        MarkerRig rig;
        Player* live = FindPlayer(scene.Objects());
        EXPECT_NE(live, nullptr);
        if (live != nullptr)
        {
            rig.movement = live->FindComponent<NS::Game::Player::PlayerComponent>();
            rig.input = live->FindComponent<LevelNs::CollisionInput>();
            rig.marker = live->FindComponent<LevelNs::TargetMarker>();
            // 起こしたままだと実機の入力が毎フレーム 0 を書き込み、試しが置いた狙いの向きが消える
            if (SceneNs::PlayerInput* input = live->FindComponent<SceneNs::PlayerInput>())
            {
                input->SetActive(false);
            }
        }
        scene.Objects().ForEachComponent<LevelNs::Breakable>(
            [&rig](LevelNs::Breakable& breakable) { rig.target = breakable.Owner(); });
        return rig;
    }

    // 帯の範囲は半開なので Update (200) の移動は入らない。移動は自機の分だけ回す
    void Step(SceneNs::Scene& scene, const MarkerRig& rig)
    {
        scene.Objects().UpdateObjects(SceneNs::TickPriority::EarlyUpdate, SceneNs::TickPriority::Update);
        if (rig.movement != nullptr)
        {
            rig.movement->OnUpdate();
        }
    }

    // 床へ着けてから +X を狙う。落下が混ざると狙う相手を探す位置がフレームごとに動く
    void SettleAndAimAhead(SceneNs::Scene& scene, const MarkerRig& rig)
    {
        for (int i = 0; i < 30 && !rig.movement->IsGrounded(); ++i)
        {
            Step(scene, rig);
        }
        rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    }

    // 自機の斜め後ろの上から的を見る。行列の定数は他の翻訳単位の初期化を待たずに作られうるので、呼ぶたびに作る
    [[nodiscard]] NS::Core::Matrix ChaseView()
    {
        return LookFrom(Vector3{-4.0f, 3.0f, 0.0f}, Vector3{5.0f, 1.0f, 0.0f});
    }

    // 質量の違う的を置き、溜め量 0.3 と溜めきりで組んだ形を返す
    std::pair<LevelNs::TargetMarkerShape, LevelNs::TargetMarkerShape> ShapesAtPartAndFullCharge(float targetMass)
    {
        SceneNs::Scene scene;
        MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{.targetMass = targetMass});
        LevelNs::TargetMarkerShape part{};
        LevelNs::TargetMarkerShape full{};
        EXPECT_NE(rig.marker, nullptr);
        EXPECT_NE(rig.input, nullptr);
        if (rig.marker == nullptr || rig.input == nullptr)
        {
            return {part, full};
        }
        SettleAndAimAhead(scene, rig);

        MouseLeftPress press;
        for (int i = 0; i < 120 && rig.input->Judge().Charge01() < 0.3f; ++i)
        {
            Step(scene, rig);
        }
        EXPECT_TRUE(rig.input->IsCharging());
        EXPECT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, part));
        for (int i = 0; i < 120 && !rig.input->IsChargeFull(); ++i)
        {
            Step(scene, rig);
        }
        EXPECT_TRUE(rig.input->IsChargeFull());
        EXPECT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, full));
        return {part, full};
    }
} // namespace

// 相手を画面の中央に置くと、投げた矩形の四隅に横と縦の 2 本ずつが付き、腕は矩形の短い辺の 0.25
TEST(TargetMarker, CornerHooksSitOnTheProjectedBoxCorners)
{
    const NS::Core::Matrix viewProjection = LookFrom(Vector3{0.0f, 1.0f, 0.0f}, Vector3{5.0f, 1.0f, 0.0f});
    const LevelNs::SlamLineTarget target = LineTargetAhead(0.0f, 0.0f);

    LevelNs::TargetMarkerShape shape{};
    ASSERT_TRUE(LevelNs::BuildTargetMarkerShape(
        viewProjection, k_TargetSize, target, k_BallRadius, LevelNs::TargetMarkerDesc{}, shape));

    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();
    for (int corner = 0; corner < 8; ++corner)
    {
        float sx = -1.0f;
        if ((corner & 1) != 0)
        {
            sx = 1.0f;
        }
        float sy = -1.0f;
        if ((corner & 2) != 0)
        {
            sy = 1.0f;
        }
        float sz = -1.0f;
        if ((corner & 4) != 0)
        {
            sz = 1.0f;
        }
        const Vector3 point =
            Vector3(target.bounds.Center) +
            Vector3{sx * target.bounds.Extents.x, sy * target.bounds.Extents.y, sz * target.bounds.Extents.z};
        const NS::Core::Vector2 pixel = ToPixels(viewProjection, point);
        left = std::min(left, pixel.x);
        right = std::max(right, pixel.x);
        top = std::min(top, pixel.y);
        bottom = std::max(bottom, pixel.y);
    }
    // 相手は画面の中央
    ASSERT_NEAR((left + right) * 0.5f, 640.0f, 0.01f);
    ASSERT_NEAR((top + bottom) * 0.5f, 360.0f, 0.01f);

    const float arm = 0.25f * std::min(right - left, bottom - top);
    const float thickness = 3.0f;
    const LevelNs::TargetMarkerShape expected{
        .corners =
            {
                LevelNs::MarkerRect{left, top, arm, thickness},
                LevelNs::MarkerRect{left, top, thickness, arm},
                LevelNs::MarkerRect{right - arm, top, arm, thickness},
                LevelNs::MarkerRect{right - thickness, top, thickness, arm},
                LevelNs::MarkerRect{left, bottom - thickness, arm, thickness},
                LevelNs::MarkerRect{left, bottom - arm, thickness, arm},
                LevelNs::MarkerRect{right - arm, bottom - thickness, arm, thickness},
                LevelNs::MarkerRect{right - thickness, bottom - arm, thickness, arm},
            },
        .dots = {},
    };
    ASSERT_EQ(shape.corners.size(), expected.corners.size());
    for (std::size_t i = 0; i < expected.corners.size(); ++i)
    {
        ExpectRectNear(shape.corners[i], expected.corners[i], i);
    }
}

// 道筋の点は玉の半径の先から 0.5 m ごとに並び、線の上で相手の中心に一番近い所で終わる。相手の先へ伸びない
// 横ずれ 0 なら終わりの点は相手の中心に重なり、横ずれ 0.5 なら相手の中心から横へ開く
TEST(TargetMarker, PathDotsRunFromTheBallToBesideTheTargetCenter)
{
    const LevelNs::SlamLineTarget centered = LineTargetAhead(0.0f, 0.0f);
    LevelNs::TargetMarkerShape shape{};
    ASSERT_TRUE(LevelNs::BuildTargetMarkerShape(
        ChaseView(), k_TargetSize, centered, k_BallRadius, LevelNs::TargetMarkerDesc{}, shape));

    // 5 m から 0.5 m ずつ手前へ、玉の半径 0.65 m より先の 1.0 m まで
    std::vector<float> expectedAlong;
    for (int k = 8; k >= 0; --k)
    {
        expectedAlong.push_back(5.0f - 0.5f * static_cast<float>(k));
    }
    ASSERT_EQ(shape.dots.size(), expectedAlong.size());
    for (std::size_t i = 0; i < expectedAlong.size(); ++i)
    {
        const NS::Core::Vector2 pixel = ToPixels(ChaseView(), Vector3{expectedAlong[i], 1.0f, 0.0f});
        ExpectRectNear(shape.dots[i], LevelNs::MarkerRect{pixel.x - 3.0f, pixel.y - 3.0f, 6.0f, 6.0f}, i);
    }
    const NS::Core::Vector2 centerPixel = ToPixels(ChaseView(), centered.bounds.Center);
    const NS::Core::Vector2 lastCentered = RectCenter(shape.dots.back());
    EXPECT_NEAR(lastCentered.x, centerPixel.x, k_Tolerance);
    EXPECT_NEAR(lastCentered.y, centerPixel.y, k_Tolerance);

    // 横ずれ 0.5 は、相手の半幅 0.5 と玉の半径 0.65 の和 1.15 の半分だけ横へずらした相手
    const LevelNs::SlamLineTarget offset = LineTargetAhead(0.575f, 0.5f);
    LevelNs::TargetMarkerShape offsetShape{};
    ASSERT_TRUE(LevelNs::BuildTargetMarkerShape(
        ChaseView(), k_TargetSize, offset, k_BallRadius, LevelNs::TargetMarkerDesc{}, offsetShape));
    ASSERT_FALSE(offsetShape.dots.empty());
    const NS::Core::Vector2 offsetCenterPixel = ToPixels(ChaseView(), offset.bounds.Center);
    EXPECT_GT(std::abs(RectCenter(offsetShape.dots.back()).x - offsetCenterPixel.x), 10.0f);
}

// 相手の無い線の点は玉の半径の先から 0.5 m ごとに並び、突進が止まる所で終わる。印は組まない
TEST(TargetMarker, AimPathDotsRunFromTheBallToTheEndOfTheSlam)
{
    const LevelNs::AimLine line{
        .origin = Vector3{0.0f, 1.0f, 0.0f}, .direction = Vector3{1.0f, 0.0f, 0.0f}, .length = 10.0f};
    LevelNs::TargetMarkerShape shape{};
    ASSERT_TRUE(
        LevelNs::BuildAimPathShape(ChaseView(), k_TargetSize, line, k_BallRadius, LevelNs::TargetMarkerDesc{}, shape));

    EXPECT_TRUE(shape.corners.empty());
    // 10 m から 0.5 m ずつ手前へ、玉の半径 0.65 m より先の 1.0 m まで
    std::vector<float> expectedAlong;
    for (int k = 18; k >= 0; --k)
    {
        expectedAlong.push_back(10.0f - 0.5f * static_cast<float>(k));
    }
    ASSERT_EQ(shape.dots.size(), expectedAlong.size());
    for (std::size_t i = 0; i < expectedAlong.size(); ++i)
    {
        const NS::Core::Vector2 pixel = ToPixels(ChaseView(), Vector3{expectedAlong[i], 1.0f, 0.0f});
        ExpectRectNear(shape.dots[i], LevelNs::MarkerRect{pixel.x - 3.0f, pixel.y - 3.0f, 6.0f, 6.0f}, i);
    }
}

// カメラの後ろの相手には印を組まない。道筋の点もカメラの後ろなら組まない
TEST(TargetMarker, NothingIsBuiltBehindTheCamera)
{
    const NS::Core::Matrix lookingAway = LookFrom(Vector3{0.0f, 1.0f, 0.0f}, Vector3{-5.0f, 1.0f, 0.0f});
    LevelNs::TargetMarkerShape shape{};
    ASSERT_TRUE(LevelNs::BuildTargetMarkerShape(
        lookingAway, k_TargetSize, LineTargetAhead(0.0f, 0.0f), k_BallRadius, LevelNs::TargetMarkerDesc{}, shape));
    EXPECT_TRUE(shape.corners.empty());
    EXPECT_TRUE(shape.dots.empty());
}

// 欄の壊れた値 (0 以下・非数) では組まずに偽を返し、結果を書き換えない
TEST(TargetMarker, BrokenFieldsBuildNothing)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const LevelNs::TargetMarkerDesc broken[] = {
        LevelNs::TargetMarkerDesc{.lineThickness = -1.0f},
        LevelNs::TargetMarkerDesc{.armRatio = nan},
        LevelNs::TargetMarkerDesc{.dotSpacing = 0.0f},
        LevelNs::TargetMarkerDesc{.dotSize = -6.0f},
    };
    for (const LevelNs::TargetMarkerDesc& desc : broken)
    {
        LevelNs::TargetMarkerShape shape{.corners = {LevelNs::MarkerRect{1.0f, 2.0f, 3.0f, 4.0f}}, .dots = {}};
        EXPECT_FALSE(LevelNs::BuildTargetMarkerShape(
            ChaseView(), k_TargetSize, LineTargetAhead(0.0f, 0.0f), k_BallRadius, desc, shape));
        ASSERT_EQ(shape.corners.size(), 1u);
        EXPECT_FLOAT_EQ(shape.corners[0].x, 1.0f);
        EXPECT_TRUE(shape.dots.empty());
    }
}

// 示す相手は溜めている間の狙う相手だけ。押しているが溜めに入る前と放した後は示さない
TEST(TargetMarker, ShowsTheAimTargetOnlyWhileCharging)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{});
    ASSERT_NE(rig.marker, nullptr);
    ASSERT_NE(rig.input, nullptr);
    ASSERT_NE(rig.target, nullptr);
    SettleAndAimAhead(scene, rig);
    LevelNs::TargetMarkerShape shape{};

    Step(scene, rig);
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());

    MouseLeftPress press;
    Step(scene, rig);
    LevelNs::SlamLineTarget aim{};
    ASSERT_TRUE(rig.input->Judge().IsHeld());
    ASSERT_FALSE(rig.input->IsCharging());
    ASSERT_TRUE(rig.input->TryGetAimTarget(aim));
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());
    EXPECT_FALSE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, shape));

    for (int i = 0; i < 20 && !rig.input->IsCharging(); ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.input->IsCharging());
    EXPECT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.target->Id()});
    EXPECT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, shape));

    press.Release();
    Step(scene, rig);
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());
    LevelNs::TargetMarkerShape afterRelease{};
    EXPECT_FALSE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, afterRelease));
}

// 溜めている間は、狙いの線の上に相手がいない横と後ろへ倒しても、狙いの向きへ突進が止まる所まで点を出す
TEST(TargetMarker, ShowsAimPathDotsWithoutATargetWhileCharging)
{
    // 自機の後ろの高い所から見下ろす。横と後ろの線がどちらもカメラの前に入る
    const NS::Core::Matrix overhead = LookFrom(Vector3{-14.0f, 24.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f});
    for (const Vector3& aim : {Vector3{0.0f, 0.0f, 1.0f}, Vector3{-1.0f, 0.0f, 0.0f}})
    {
        SceneNs::Scene scene;
        MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{});
        ASSERT_NE(rig.marker, nullptr);
        ASSERT_NE(rig.input, nullptr);
        SettleAndAimAhead(scene, rig);
        rig.movement->SetDesiredMove(aim, 0.0f);

        MouseLeftPress press;
        for (int i = 0; i < 20 && !rig.input->IsCharging(); ++i)
        {
            Step(scene, rig);
        }
        ASSERT_TRUE(rig.input->IsCharging());
        EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet()) << aim.x;

        LevelNs::AimLine line{};
        ASSERT_TRUE(rig.input->TryGetAimLine(line)) << aim.x;
        const Vector3 position = rig.movement->Owner()->Root().Position();
        EXPECT_NEAR(line.origin.x, position.x, 1e-2f) << aim.x;
        EXPECT_NEAR(line.origin.z, position.z, 1e-2f) << aim.x;
        EXPECT_NEAR(line.direction.x, aim.x, 1e-5f) << aim.x;
        EXPECT_NEAR(line.direction.z, aim.z, 1e-5f) << aim.x;
        EXPECT_FLOAT_EQ(line.length, rig.movement->BodySlamDistance()) << aim.x;

        LevelNs::TargetMarkerShape shape{};
        ASSERT_TRUE(rig.marker->BuildShownShape(overhead, k_TargetSize, shape)) << aim.x;
        EXPECT_TRUE(shape.corners.empty()) << aim.x;
        ASSERT_FALSE(shape.dots.empty()) << aim.x;
        const NS::Core::Vector2 lastExpected = ToPixels(overhead, line.origin + line.direction * line.length);
        const NS::Core::Vector2 lastActual = RectCenter(shape.dots.back());
        EXPECT_NEAR(lastActual.x, lastExpected.x, k_Tolerance) << aim.x;
        EXPECT_NEAR(lastActual.y, lastExpected.y, k_Tolerance) << aim.x;
        const float firstAlong =
            line.length - 0.5f * std::floor((line.length - rig.movement->CapsuleRadius()) / 0.5f);
        const NS::Core::Vector2 firstExpected = ToPixels(overhead, line.origin + line.direction * firstAlong);
        const NS::Core::Vector2 firstActual = RectCenter(shape.dots.front());
        EXPECT_NEAR(firstActual.x, firstExpected.x, k_Tolerance) << aim.x;
        EXPECT_NEAR(firstActual.y, firstExpected.y, k_Tolerance) << aim.x;
    }
}

// 同じ配置物に CollisionInput が無ければ、押し続けても何も示さない
TEST(TargetMarker, ShowsNothingWithoutCollisionInput)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{.withCollisionInput = false});
    ASSERT_NE(rig.marker, nullptr);
    ASSERT_EQ(rig.input, nullptr);
    SettleAndAimAhead(scene, rig);

    MouseLeftPress press;
    for (int i = 0; i < 30; ++i)
    {
        Step(scene, rig);
    }
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());
    LevelNs::TargetMarkerShape shape{};
    EXPECT_FALSE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, shape));
}

// 印と道筋の点の形は、溜め量 0.3 と溜めきり、質量 0.5 と 8 の相手で同じ
TEST(TargetMarker, ShapeDoesNotChangeWithChargeOrMass)
{
    const std::pair<LevelNs::TargetMarkerShape, LevelNs::TargetMarkerShape> light = ShapesAtPartAndFullCharge(0.5f);
    const std::pair<LevelNs::TargetMarkerShape, LevelNs::TargetMarkerShape> heavy = ShapesAtPartAndFullCharge(8.0f);

    ASSERT_EQ(light.first.corners.size(), 8u);
    ASSERT_FALSE(light.first.dots.empty());
    ExpectShapeNear(light.second, light.first);
    ExpectShapeNear(heavy.first, light.first);
    ExpectShapeNear(heavy.second, light.first);
}

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
// 受け取るフレーム数は捉えてから・外れてからの時間
static_assert(std::is_same_v<decltype(&LevelNs::BuildLockOnFrame),
                             bool (*)(const NS::Core::Matrix&,
                                      NS::Core::Size2D,
                                      const NS::Core::AABB&,
                                      LevelNs::LockOnFrames,
                                      const LevelNs::TargetMarkerDesc&,
                                      LevelNs::LockOnFrameShape&)>);
static_assert(std::is_same_v<decltype(&LevelNs::BuildTargetMarkerShape),
                             bool (*)(const NS::Core::Matrix&,
                                      NS::Core::Size2D,
                                      const LevelNs::SlamLineTarget&,
                                      float,
                                      int,
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
    // 既定の欄で枠が縮み切る、捉えてからのフレーム数
    constexpr int k_SettledFrames = 6;

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

    void ExpectColorNear(const NS::Core::Color& actual, const NS::Core::Color& expected)
    {
        EXPECT_NEAR(actual.R(), expected.R(), k_Tolerance);
        EXPECT_NEAR(actual.G(), expected.G(), k_Tolerance);
        EXPECT_NEAR(actual.B(), expected.B(), k_Tolerance);
        EXPECT_NEAR(actual.A(), expected.A(), k_Tolerance);
    }

    void ExpectFrameNear(const LevelNs::LockOnFrameShape& actual, const LevelNs::LockOnFrameShape& expected)
    {
        ASSERT_EQ(actual.corners.size(), expected.corners.size());
        ASSERT_EQ(actual.outline.size(), expected.outline.size());
        for (std::size_t i = 0; i < expected.corners.size(); ++i)
        {
            ExpectRectNear(actual.corners[i], expected.corners[i], i);
        }
        for (std::size_t i = 0; i < expected.outline.size(); ++i)
        {
            ExpectRectNear(actual.outline[i], expected.outline[i], i);
        }
        ExpectColorNear(actual.color, expected.color);
        ExpectColorNear(actual.outlineColor, expected.outlineColor);
    }

    void ExpectShapeNear(const LevelNs::TargetMarkerShape& actual, const LevelNs::TargetMarkerShape& expected)
    {
        ExpectFrameNear(actual.frame, expected.frame);
        ASSERT_EQ(actual.dots.size(), expected.dots.size());
        for (std::size_t i = 0; i < expected.dots.size(); ++i)
        {
            ExpectRectNear(actual.dots[i], expected.dots[i], i);
        }
    }

    // 枠の明るい線から読んだ、囲む正方形の左上と一辺
    struct FrameSquare
    {
        float left = 0.0f;
        float top = 0.0f;
        float side = 0.0f;
    };

    // 左上の横の線の左端と、右上の横の線の右端で測る
    [[nodiscard]] FrameSquare ReadFrameSquare(const LevelNs::LockOnFrameShape& frame)
    {
        FrameSquare measured{};
        EXPECT_EQ(frame.corners.size(), 8u);
        if (frame.corners.size() != 8u)
        {
            return measured;
        }
        measured.left = frame.corners[0].x;
        measured.top = frame.corners[0].y;
        measured.side = frame.corners[2].x + frame.corners[2].width - frame.corners[0].x;
        return measured;
    }

    // 左上 (left, top)・一辺 side の正方形の四隅に、腕 arm・太さ thickness のかぎ形を置いた明るい線
    [[nodiscard]] std::vector<LevelNs::MarkerRect> SquareCorners(float left,
                                                                 float top,
                                                                 float side,
                                                                 float arm,
                                                                 float thickness)
    {
        const float right = left + side;
        const float bottom = top + side;
        return {
            LevelNs::MarkerRect{left, top, arm, thickness},
            LevelNs::MarkerRect{left, top, thickness, arm},
            LevelNs::MarkerRect{right - arm, top, arm, thickness},
            LevelNs::MarkerRect{right - thickness, top, thickness, arm},
            LevelNs::MarkerRect{left, bottom - thickness, arm, thickness},
            LevelNs::MarkerRect{left, bottom - arm, thickness, arm},
            LevelNs::MarkerRect{right - arm, bottom - thickness, arm, thickness},
            LevelNs::MarkerRect{right - thickness, bottom - arm, thickness, arm},
        };
    }

    // 自機の玉の中心の高さから +X を水平に見る。上へずらした点は中心と同じ奥行きにあるので、画素の半径をそのまま測れる
    [[nodiscard]] NS::Core::Matrix LevelView()
    {
        return LookFrom(Vector3{0.0f, 1.0f, 0.0f}, Vector3{5.0f, 1.0f, 0.0f});
    }

    // 水平に見た時の、中心 center・半径 radius の相手を捉えている間の枠の一辺。輪郭との間は 6 画素で、下限は入れない
    [[nodiscard]] float SettledSideWithoutMinimum(const Vector3& center, float radius)
    {
        const float centerY = ToPixels(LevelView(), center).y;
        const float topY = ToPixels(LevelView(), center + Vector3{0.0f, radius, 0.0f}).y;
        return 2.0f * ((centerY - topY) + 6.0f);
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
        bool withSideTarget = false; // (5, 1, 3) に 2 つ目の的を置くか
    };

    struct MarkerRig
    {
        NS::Game::Player::PlayerComponent* movement = nullptr;
        LevelNs::CollisionInput* input = nullptr;
        LevelNs::TargetMarker* marker = nullptr;
        SceneNs::GameObject* target = nullptr;
        SceneNs::GameObject* sideTarget = nullptr;
    };

    // (5, 1, z) の升に置く、動かない壊せる的
    [[nodiscard]] nlohmann::json MakeTargetObject(std::int16_t z, float mass)
    {
        nlohmann::json target = NS::Editor::MakeCellObject(5, 1, z);
        nlohmann::json rigidBody = SceneNs::MakeComponentEntry("RigidBody");
        SceneNs::SetField(rigidBody, "キネマティック", true);
        SceneNs::SetField(rigidBody, "質量", mass);
        SceneNs::ObjectJsonComponents(target).push_back(rigidBody);
        SceneNs::ObjectJsonComponents(target).push_back(SceneNs::MakeComponentEntry("Breakable"));
        return target;
    }

    // 自機を原点に置き、+X の 5 m 先の床の上に壊せる的を 1 体置く。withSideTarget なら (5, 1, 3) にもう 1 体置く
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

        SceneNs::SceneJsonObjects(data).push_back(MakeTargetObject(0, course.targetMass));
        if (course.withSideTarget)
        {
            SceneNs::SceneJsonObjects(data).push_back(MakeTargetObject(3, course.targetMass));
        }
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
        scene.Objects().ForEachComponent<LevelNs::Breakable>([&rig](LevelNs::Breakable& breakable) {
            if (breakable.Owner()->Root().Position().z > 1.5f)
            {
                rig.sideTarget = breakable.Owner();
                return;
            }
            rig.target = breakable.Owner();
        });
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

    // 押して溜めに入り、枠が縮み切るまで回す
    void ChargeUntilTheFrameSettles(SceneNs::Scene& scene, const MarkerRig& rig)
    {
        for (int i = 0; i < 20 && !rig.input->IsCharging(); ++i)
        {
            Step(scene, rig);
        }
        ASSERT_TRUE(rig.input->IsCharging());
        for (int i = 0; i < k_SettledFrames; ++i)
        {
            Step(scene, rig);
        }
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

// 枠は相手の中心を囲む正方形で、一辺は 2 × (外接箱の半分の長さの最大をその奥行きで投げた画素 + 6 画素)。
// 四隅に横と縦の 2 本ずつが付き、腕は一辺の 0.25。暗い縁は明るい線の各四角を両側 1 画素ずつ広げる
TEST(TargetMarker, FrameIsASquareAroundTheTargetRadius)
{
    LevelNs::SlamLineTarget target = LineTargetAhead(0.0f, 0.0f);
    // 半分の長さが軸ごとに違う箱。一番長い縦の 0.5 が半径になる
    target.bounds = NS::Core::AABB{Vector3{5.0f, 1.0f, 0.0f}, Vector3{0.3f, 0.5f, 0.2f}};

    LevelNs::TargetMarkerShape shape{};
    ASSERT_TRUE(LevelNs::BuildTargetMarkerShape(
        LevelView(), k_TargetSize, target, k_BallRadius, k_SettledFrames, LevelNs::TargetMarkerDesc{}, shape));

    const NS::Core::Vector2 center = ToPixels(LevelView(), target.bounds.Center);
    // 相手は画面の中央
    ASSERT_NEAR(center.x, 640.0f, 0.01f);
    ASSERT_NEAR(center.y, 360.0f, 0.01f);
    const float side = SettledSideWithoutMinimum(target.bounds.Center, 0.5f);
    // 下限の 70 画素より大きい相手で見る
    ASSERT_GT(side, 100.0f);

    const float arm = 0.25f * side;
    const float thickness = 3.0f;
    LevelNs::LockOnFrameShape expected{};
    expected.corners = SquareCorners(center.x - side * 0.5f, center.y - side * 0.5f, side, arm, thickness);
    for (const LevelNs::MarkerRect& rect : expected.corners)
    {
        expected.outline.push_back(
            LevelNs::MarkerRect{rect.x - 1.0f, rect.y - 1.0f, rect.width + 2.0f, rect.height + 2.0f});
    }
    expected.color = NS::Core::Color{1.0f, 0.85f, 0.2f, 0.9f};
    expected.outlineColor = NS::Core::Color{0.1f, 0.08f, 0.02f, 0.6f};
    ExpectFrameNear(shape.frame, expected);
}

// 遠くの小さな相手でも、枠の一辺は 70 画素を下回らない。中心は相手の中心のまま
TEST(TargetMarker, FrameSideDoesNotGoBelowTheMinimum)
{
    const NS::Core::AABB farBox{Vector3{20.0f, 1.0f, 0.0f}, Vector3{0.1f, 0.1f, 0.1f}};
    ASSERT_LT(SettledSideWithoutMinimum(farBox.Center, 0.1f), 70.0f);

    LevelNs::LockOnFrameShape frame{};
    ASSERT_TRUE(LevelNs::BuildLockOnFrame(LevelView(),
                                          k_TargetSize,
                                          farBox,
                                          LevelNs::LockOnFrames{.sinceCapture = k_SettledFrames},
                                          LevelNs::TargetMarkerDesc{},
                                          frame));
    const FrameSquare measured = ReadFrameSquare(frame);
    EXPECT_NEAR(measured.side, 70.0f, k_Tolerance);
    const NS::Core::Vector2 center = ToPixels(LevelView(), farBox.Center);
    EXPECT_NEAR(measured.left + measured.side * 0.5f, center.x, k_Tolerance);
    EXPECT_NEAR(measured.top + measured.side * 0.5f, center.y, k_Tolerance);
}

// 捉えたフレームは一辺の 5 倍 (465 画素が上限)・白・不透明度 0.35 で出て、6 フレームで一辺 1 倍・印の色・不透明度 0.9 へ縮む。
// 進みは 0.12・0.38・0.70・1.0 を 6 フレームの 1/4 ずつに置いて直線でつないだ曲線。暗い縁の不透明度は枠の不透明度に比例する
TEST(TargetMarker, CapturedFrameShrinksFromFiveTimesWhiteOverSixFrames)
{
    const NS::Core::AABB nearBox{Vector3{5.0f, 1.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}};
    const float settled = SettledSideWithoutMinimum(nearBox.Center, 0.5f);
    // 5 倍は上限の 465 画素を超える
    ASSERT_GT(settled * 5.0f, 465.0f);
    const NS::Core::Vector2 center = ToPixels(LevelView(), nearBox.Center);

    // 捉えたフレームを 0 とした 0〜7 フレーム目の進み
    const float progress[] = {
        0.0f, 0.08f, 0.12f + 0.26f / 3.0f, 0.38f, 0.38f + 0.32f * 2.0f / 3.0f, 0.70f + 0.30f / 3.0f, 1.0f, 1.0f};
    for (int f = 0; f < 8; ++f)
    {
        const float p = progress[f];
        LevelNs::LockOnFrameShape frame{};
        ASSERT_TRUE(LevelNs::BuildLockOnFrame(LevelView(),
                                              k_TargetSize,
                                              nearBox,
                                              LevelNs::LockOnFrames{.sinceCapture = f},
                                              LevelNs::TargetMarkerDesc{},
                                              frame));
        const FrameSquare measured = ReadFrameSquare(frame);
        EXPECT_NEAR(measured.side, 465.0f + (settled - 465.0f) * p, 1e-2f) << f;
        EXPECT_NEAR(measured.left + measured.side * 0.5f, center.x, 1e-2f) << f;
        EXPECT_NEAR(measured.top + measured.side * 0.5f, center.y, 1e-2f) << f;
        const float alpha = 0.35f + (0.9f - 0.35f) * p;
        ExpectColorNear(frame.color,
                        NS::Core::Color{1.0f, 1.0f + (0.85f - 1.0f) * p, 1.0f + (0.2f - 1.0f) * p, alpha});
        EXPECT_NEAR(frame.outlineColor.A(), 0.6f * alpha / 0.9f, k_Tolerance) << f;
    }

    // 上限に届かない小さな相手は、下限の一辺 70 画素の 5 倍で出る
    const NS::Core::AABB farBox{Vector3{20.0f, 1.0f, 0.0f}, Vector3{0.1f, 0.1f, 0.1f}};
    LevelNs::LockOnFrameShape farFrame{};
    ASSERT_TRUE(LevelNs::BuildLockOnFrame(LevelView(),
                                          k_TargetSize,
                                          farBox,
                                          LevelNs::LockOnFrames{.sinceCapture = 0},
                                          LevelNs::TargetMarkerDesc{},
                                          farFrame));
    EXPECT_NEAR(ReadFrameSquare(farFrame).side, 350.0f, 1e-2f);
}

// 道筋の点は玉の半径の先から 0.5 m ごとに並び、線の上で相手の中心に一番近い所で終わる。相手の先へ伸びない
// 横ずれ 0 なら終わりの点は相手の中心に重なり、横ずれ 0.5 なら相手の中心から横へ開く
TEST(TargetMarker, PathDotsRunFromTheBallToBesideTheTargetCenter)
{
    const LevelNs::SlamLineTarget centered = LineTargetAhead(0.0f, 0.0f);
    LevelNs::TargetMarkerShape shape{};
    ASSERT_TRUE(LevelNs::BuildTargetMarkerShape(
        ChaseView(), k_TargetSize, centered, k_BallRadius, k_SettledFrames, LevelNs::TargetMarkerDesc{}, shape));

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
        ChaseView(), k_TargetSize, offset, k_BallRadius, k_SettledFrames, LevelNs::TargetMarkerDesc{}, offsetShape));
    ASSERT_FALSE(offsetShape.dots.empty());
    const NS::Core::Vector2 offsetCenterPixel = ToPixels(ChaseView(), offset.bounds.Center);
    EXPECT_GT(std::abs(RectCenter(offsetShape.dots.back()).x - offsetCenterPixel.x), 10.0f);
}

// 相手の無い線の点は玉の半径の先から 0.5 m ごとに並び、突進が止まる所で終わる。枠は組まない
TEST(TargetMarker, AimPathDotsRunFromTheBallToTheEndOfTheSlam)
{
    const LevelNs::AimLine line{
        .origin = Vector3{0.0f, 1.0f, 0.0f}, .direction = Vector3{1.0f, 0.0f, 0.0f}, .length = 10.0f};
    LevelNs::TargetMarkerShape shape{};
    ASSERT_TRUE(
        LevelNs::BuildAimPathShape(ChaseView(), k_TargetSize, line, k_BallRadius, LevelNs::TargetMarkerDesc{}, shape));

    EXPECT_TRUE(shape.frame.corners.empty());
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

// カメラの後ろの相手には枠を組まない。道筋の点もカメラの後ろなら組まない
TEST(TargetMarker, NothingIsBuiltBehindTheCamera)
{
    const NS::Core::Matrix lookingAway = LookFrom(Vector3{0.0f, 1.0f, 0.0f}, Vector3{-5.0f, 1.0f, 0.0f});
    LevelNs::TargetMarkerShape shape{};
    ASSERT_TRUE(LevelNs::BuildTargetMarkerShape(lookingAway,
                                                k_TargetSize,
                                                LineTargetAhead(0.0f, 0.0f),
                                                k_BallRadius,
                                                k_SettledFrames,
                                                LevelNs::TargetMarkerDesc{},
                                                shape));
    EXPECT_TRUE(shape.frame.corners.empty());
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
        LevelNs::TargetMarkerDesc{.frameMinSide = -1.0f},
        LevelNs::TargetMarkerDesc{.frameAlpha = 0.0f},
        LevelNs::TargetMarkerDesc{.appearFrames = -1},
        LevelNs::TargetMarkerDesc{.lostFrames = -1},
        LevelNs::TargetMarkerDesc{.outlineAlpha = nan},
    };
    for (const LevelNs::TargetMarkerDesc& desc : broken)
    {
        LevelNs::TargetMarkerShape shape{.frame = {.corners = {LevelNs::MarkerRect{1.0f, 2.0f, 3.0f, 4.0f}}},
                                         .dots = {}};
        EXPECT_FALSE(LevelNs::BuildTargetMarkerShape(
            ChaseView(), k_TargetSize, LineTargetAhead(0.0f, 0.0f), k_BallRadius, k_SettledFrames, desc, shape));
        ASSERT_EQ(shape.frame.corners.size(), 1u);
        EXPECT_FLOAT_EQ(shape.frame.corners[0].x, 1.0f);
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
        EXPECT_TRUE(shape.frame.corners.empty()) << aim.x;
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

// 溜めている間に狙う相手が別の相手へ替わったフレームに、枠は新しい相手の上へ縮み切った形で移る。捉えた瞬間の縮みを繰り返さない
TEST(TargetMarker, SwitchingTargetsMovesTheFrameWithoutShrinkingAgain)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{.withSideTarget = true});
    ASSERT_NE(rig.marker, nullptr);
    ASSERT_NE(rig.input, nullptr);
    ASSERT_NE(rig.target, nullptr);
    ASSERT_NE(rig.sideTarget, nullptr);
    SettleAndAimAhead(scene, rig);

    MouseLeftPress press;
    ChargeUntilTheFrameSettles(scene, rig);
    ASSERT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.target->Id()});

    // 横の的へ 1 フレームで向ける。間に相手のいないフレームを挟まない
    Vector3 toSide{5.0f, 0.0f, 3.0f};
    toSide.Normalize();
    rig.movement->SetDesiredMove(toSide, 0.0f);
    Step(scene, rig);
    ASSERT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.sideTarget->Id()});
    LevelNs::TargetMarkerShape switched{};
    ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, switched));

    for (int i = 0; i < k_SettledFrames + 2; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.sideTarget->Id()});
    LevelNs::TargetMarkerShape settled{};
    ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, settled));
    ASSERT_EQ(settled.frame.corners.size(), 8u);
    ExpectFrameNear(switched.frame, settled.frame);
}

// 溜めている間に狙う相手が外れると、外れたフレームから 2 フレーム、直前の枠を 0.9 倍の一辺で同じ色のまま出して消す。
// その 2 フレームは示す相手を未設定にする。道筋の点は狙いの線の先まで出たまま
TEST(TargetMarker, LostTargetLeavesTheFrameForTwoFramesWithoutAShownTarget)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{});
    ASSERT_NE(rig.marker, nullptr);
    ASSERT_NE(rig.input, nullptr);
    ASSERT_NE(rig.target, nullptr);
    SettleAndAimAhead(scene, rig);

    MouseLeftPress press;
    ChargeUntilTheFrameSettles(scene, rig);
    ASSERT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.target->Id()});
    LevelNs::TargetMarkerShape held{};
    ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, held));
    const FrameSquare heldSquare = ReadFrameSquare(held.frame);

    rig.movement->SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 0.0f);
    for (int lost = 0; lost < 2; ++lost)
    {
        Step(scene, rig);
        ASSERT_TRUE(rig.input->IsCharging());
        EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet()) << lost;
        LevelNs::TargetMarkerShape shape{};
        ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, shape)) << lost;
        EXPECT_FALSE(shape.dots.empty()) << lost;
        const FrameSquare measured = ReadFrameSquare(shape.frame);
        EXPECT_NEAR(measured.side, heldSquare.side * 0.9f, 1e-2f) << lost;
        EXPECT_NEAR(measured.left + measured.side * 0.5f, heldSquare.left + heldSquare.side * 0.5f, 1e-2f) << lost;
        EXPECT_NEAR(measured.top + measured.side * 0.5f, heldSquare.top + heldSquare.side * 0.5f, 1e-2f) << lost;
        ExpectColorNear(shape.frame.color, held.frame.color);
    }

    Step(scene, rig);
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());
    LevelNs::TargetMarkerShape after{};
    ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, after));
    EXPECT_TRUE(after.frame.corners.empty());
    EXPECT_TRUE(after.frame.outline.empty());
    EXPECT_FALSE(after.dots.empty());
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

// 枠と道筋の点の形は、溜め量 0.3 と溜めきり、質量 0.5 と 8 の相手で同じ
TEST(TargetMarker, ShapeDoesNotChangeWithChargeOrMass)
{
    const std::pair<LevelNs::TargetMarkerShape, LevelNs::TargetMarkerShape> light = ShapesAtPartAndFullCharge(0.5f);
    const std::pair<LevelNs::TargetMarkerShape, LevelNs::TargetMarkerShape> heavy = ShapesAtPartAndFullCharge(8.0f);

    ASSERT_EQ(light.first.frame.corners.size(), 8u);
    ASSERT_FALSE(light.first.dots.empty());
    ExpectShapeNear(light.second, light.first);
    ExpectShapeNear(heavy.first, light.first);
    ExpectShapeNear(heavy.second, light.first);
}

#include "Editor/EditorObjects.h"
#include "Game/Player.h"
#include "camera_screen.h"

#include "hit_zones_entry.h"
#include <Game/Level/Breakable.h>
#include <Game/Level/CollisionInput.h>
#include <Game/Level/ImpactResolver.h>
#include <Game/Level/SlamArrow.h>
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

// 枠を組む関数は溜め量・威力・質量を受け取らない。受け取る口が無ければ、それらで枠を変えられない
// 受け取るフレーム数は捉えてから・外れてからの時間
static_assert(std::is_same_v<decltype(&LevelNs::BuildLockOnFrame),
                             bool (*)(const NS::Core::Matrix&,
                                      NS::Core::Size2D,
                                      const NS::Core::AABB&,
                                      LevelNs::LockOnFrames,
                                      const LevelNs::TargetMarkerDesc&,
                                      LevelNs::LockOnFrameShape&)>);

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
    [[nodiscard]] NS::Core::AABB BoxAhead()
    {
        return NS::Core::AABB{Vector3{5.0f, 1.0f, 0.0f}, Vector3{0.5f, 0.5f, 0.5f}};
    }

    // 枠が捉えている間の白。欄「印の色」の既定 (245, 247, 255) / 255
    constexpr float k_WhiteR = 245.0f / 255.0f;
    constexpr float k_WhiteG = 247.0f / 255.0f;

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
        LevelNs::SlamArrow* arrow = nullptr;
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
        NsTest::AddTestHitZones(SceneNs::ObjectJsonComponents(target));
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
        SceneNs::ObjectJsonComponents(player).push_back(SceneNs::MakeComponentEntry("SlamArrow"));
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
            rig.arrow = live->FindComponent<LevelNs::SlamArrow>();
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

    // 床へ着けてから、カメラの正面と倒す向きを +X にする。落下が混ざると狙う相手を探す位置がフレームごとに動く
    void SettleAndAimAhead(SceneNs::Scene& scene, const MarkerRig& rig)
    {
        for (int i = 0; i < 30 && !rig.movement->IsGrounded(); ++i)
        {
            Step(scene, rig);
        }
        ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
        rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    }

    // 押したまま溜めに入るフレームまで回す
    void StepUntilCharging(SceneNs::Scene& scene, const MarkerRig& rig)
    {
        for (int i = 0; i < 20 && !rig.input->IsCharging(); ++i)
        {
            Step(scene, rig);
        }
        ASSERT_TRUE(rig.input->IsCharging());
    }

    // 押して溜めに入り、枠が縮み切るまで回す
    void ChargeUntilTheFrameSettles(SceneNs::Scene& scene, const MarkerRig& rig)
    {
        StepUntilCharging(scene, rig);
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

    // 質量の違う的を置き、溜め量 0.3 と溜めきりで組んだ枠を返す
    std::pair<LevelNs::LockOnFrameShape, LevelNs::LockOnFrameShape> FramesAtPartAndFullCharge(float targetMass)
    {
        SceneNs::Scene scene;
        MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{.targetMass = targetMass});
        LevelNs::LockOnFrameShape part{};
        LevelNs::LockOnFrameShape full{};
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

// +X を狙う、玉の中心 (0, 1.15, 0) からの突進 10 m の線。狙う相手の手前の面は 10 m 先 (玉の縁が触れる所は
// 10 − 玉の半径)。矢印を出して 20 フレームで伸びきった後
[[nodiscard]] LevelNs::SlamArrowState GrownStateAhead(float charge01)
{
    return LevelNs::SlamArrowState{.line = LevelNs::AimLine{.origin = Vector3{0.0f, 1.15f, 0.0f},
                                                            .direction = Vector3{1.0f, 0.0f, 0.0f},
                                                            .length = 10.0f},
                                   .ballRadius = k_BallRadius,
                                   .targetContact = 10.0f - k_BallRadius,
                                   .framesSinceShown = 20,
                                   .charge01 = charge01};
}

void ExpectVector3Near(const Vector3& actual, const Vector3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}
} // namespace

// 枠は相手の中心を囲む正方形で、一辺は 2 × (外接箱の半分の長さの最大をその奥行きで投げた画素 + 6 画素)
// 四隅に横と縦の 2 本ずつが付き、腕は一辺の 0.25。暗い縁は明るい線の各四角を両側 1 画素ずつ広げる。色は白
TEST(TargetMarker, FrameIsASquareAroundTheTargetRadius)
{
    // 半分の長さが軸ごとに違う箱。一番長い縦の 0.5 が半径になる
    const NS::Core::AABB box{Vector3{5.0f, 1.0f, 0.0f}, Vector3{0.3f, 0.5f, 0.2f}};

    LevelNs::LockOnFrameShape frame{};
    ASSERT_TRUE(LevelNs::BuildLockOnFrame(LevelView(),
                                          k_TargetSize,
                                          box,
                                          LevelNs::LockOnFrames{.sinceCapture = k_SettledFrames},
                                          LevelNs::TargetMarkerDesc{},
                                          frame));

    const NS::Core::Vector2 center = ToPixels(LevelView(), box.Center);
    // 相手は画面の中央
    ASSERT_NEAR(center.x, 640.0f, 0.01f);
    ASSERT_NEAR(center.y, 360.0f, 0.01f);
    const float side = SettledSideWithoutMinimum(box.Center, 0.5f);
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
    expected.color = NS::Core::Color{k_WhiteR, k_WhiteG, 1.0f, 0.9f};
    expected.outlineColor = NS::Core::Color{12.0f / 255.0f, 20.0f / 255.0f, 36.0f / 255.0f, 0.6f};
    ExpectFrameNear(frame, expected);
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

// 捉えたフレームは一辺の 5 倍 (465 画素が上限)・白・不透明度 0.35 で出て、
// 6 フレームで一辺 1 倍・印の色・不透明度 0.9 へ縮む。
// 進みは 0.12・0.38・0.70・1.0 を 6 フレームの 1/4 ずつに置いて直線でつないだ曲線
// 暗い縁の不透明度は枠の不透明度に比例する
TEST(TargetMarker, CapturedFrameShrinksFromFiveTimesWhiteOverSixFrames)
{
    const NS::Core::AABB nearBox = BoxAhead();
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
                        NS::Core::Color{1.0f + (k_WhiteR - 1.0f) * p, 1.0f + (k_WhiteG - 1.0f) * p, 1.0f, alpha});
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

// カメラの後ろの相手には枠を組まない
TEST(TargetMarker, NothingIsBuiltBehindTheCamera)
{
    const NS::Core::Matrix lookingAway = LookFrom(Vector3{0.0f, 1.0f, 0.0f}, Vector3{-5.0f, 1.0f, 0.0f});
    LevelNs::LockOnFrameShape frame{};
    ASSERT_TRUE(LevelNs::BuildLockOnFrame(lookingAway,
                                          k_TargetSize,
                                          BoxAhead(),
                                          LevelNs::LockOnFrames{.sinceCapture = k_SettledFrames},
                                          LevelNs::TargetMarkerDesc{},
                                          frame));
    EXPECT_TRUE(frame.corners.empty());
    EXPECT_TRUE(frame.outline.empty());
}

// 欄の壊れた値 (0 以下・非数) では組まずに偽を返し、結果を書き換えない
TEST(TargetMarker, BrokenFieldsBuildNothing)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const LevelNs::TargetMarkerDesc broken[] = {
        LevelNs::TargetMarkerDesc{.lineThickness = -1.0f},
        LevelNs::TargetMarkerDesc{.armRatio = nan},
        LevelNs::TargetMarkerDesc{.frameMinSide = -1.0f},
        LevelNs::TargetMarkerDesc{.frameAlpha = 0.0f},
        LevelNs::TargetMarkerDesc{.appearFrames = -1},
        LevelNs::TargetMarkerDesc{.lostFrames = -1},
        LevelNs::TargetMarkerDesc{.outlineAlpha = nan},
    };
    for (const LevelNs::TargetMarkerDesc& desc : broken)
    {
        LevelNs::LockOnFrameShape frame{.corners = {LevelNs::MarkerRect{1.0f, 2.0f, 3.0f, 4.0f}}};
        EXPECT_FALSE(LevelNs::BuildLockOnFrame(ChaseView(),
                                               k_TargetSize,
                                               BoxAhead(),
                                               LevelNs::LockOnFrames{.sinceCapture = k_SettledFrames},
                                               desc,
                                               frame));
        ASSERT_EQ(frame.corners.size(), 1u);
        EXPECT_FLOAT_EQ(frame.corners[0].x, 1.0f);
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
    LevelNs::LockOnFrameShape frame{};

    Step(scene, rig);
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());

    MouseLeftPress press;
    Step(scene, rig);
    LevelNs::SlamLineTarget aim{};
    ASSERT_TRUE(rig.input->Judge().IsHeld());
    ASSERT_FALSE(rig.input->IsCharging());
    ASSERT_TRUE(rig.input->TryGetAimTarget(aim));
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());
    EXPECT_FALSE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, frame));

    StepUntilCharging(scene, rig);
    EXPECT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.target->Id()});
    EXPECT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, frame));

    press.Release();
    Step(scene, rig);
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());
    LevelNs::LockOnFrameShape afterRelease{};
    EXPECT_FALSE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, afterRelease));
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

    // カメラの正面を横の的へ 1 フレームで向ける。間に相手のいないフレームを挟まない
    Vector3 toSide{5.0f, 0.0f, 3.0f};
    toSide.Normalize();
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, toSide));
    Step(scene, rig);
    ASSERT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.sideTarget->Id()});
    LevelNs::LockOnFrameShape switched{};
    ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, switched));

    for (int i = 0; i < k_SettledFrames + 2; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.sideTarget->Id()});
    LevelNs::LockOnFrameShape settled{};
    ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, settled));
    ASSERT_EQ(settled.corners.size(), 8u);
    ExpectFrameNear(switched, settled);
}

// 溜めている間に狙う相手が外れると、外れたフレームから 2 フレーム、直前の枠を 0.9 倍の一辺で同じ色のまま出して消す
// その 2 フレームは示す相手を未設定にする
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
    LevelNs::LockOnFrameShape held{};
    ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, held));
    const FrameSquare heldSquare = ReadFrameSquare(held);

    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{0.0f, 0.0f, 1.0f}));
    for (int lost = 0; lost < 2; ++lost)
    {
        Step(scene, rig);
        ASSERT_TRUE(rig.input->IsCharging());
        EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet()) << lost;
        LevelNs::LockOnFrameShape frame{};
        ASSERT_TRUE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, frame)) << lost;
        const FrameSquare measured = ReadFrameSquare(frame);
        EXPECT_NEAR(measured.side, heldSquare.side * 0.9f, 1e-2f) << lost;
        EXPECT_NEAR(measured.left + measured.side * 0.5f, heldSquare.left + heldSquare.side * 0.5f, 1e-2f) << lost;
        EXPECT_NEAR(measured.top + measured.side * 0.5f, heldSquare.top + heldSquare.side * 0.5f, 1e-2f) << lost;
        ExpectColorNear(frame.color, held.color);
    }

    Step(scene, rig);
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());
    LevelNs::LockOnFrameShape after{};
    EXPECT_FALSE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, after));
}

// 同じ配置物に CollisionInput が無ければ、押し続けても何も示さない。地面の矢印も組まない
TEST(TargetMarker, ShowsNothingWithoutCollisionInput)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{.withCollisionInput = false});
    ASSERT_NE(rig.marker, nullptr);
    ASSERT_NE(rig.arrow, nullptr);
    ASSERT_EQ(rig.input, nullptr);
    SettleAndAimAhead(scene, rig);

    MouseLeftPress press;
    for (int i = 0; i < 30; ++i)
    {
        Step(scene, rig);
    }
    EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet());
    LevelNs::LockOnFrameShape frame{};
    EXPECT_FALSE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, frame));
    LevelNs::SlamArrowShape arrow{};
    EXPECT_FALSE(rig.arrow->TryGetShownArrow(arrow));
}

// ロックオンの枠の形は、溜め量 0.3 と溜めきり、質量 0.5 と 8 の相手で同じ
TEST(TargetMarker, ShapeDoesNotChangeWithChargeOrMass)
{
    const std::pair<LevelNs::LockOnFrameShape, LevelNs::LockOnFrameShape> light = FramesAtPartAndFullCharge(0.5f);
    const std::pair<LevelNs::LockOnFrameShape, LevelNs::LockOnFrameShape> heavy = FramesAtPartAndFullCharge(8.0f);

    ASSERT_EQ(light.first.corners.size(), 8u);
    ExpectFrameNear(light.second, light.first);
    ExpectFrameNear(heavy.first, light.first);
    ExpectFrameNear(heavy.second, light.first);
}

// 矢印は狙いの線の上に、玉の縁から相手の手前の面まで。帯の幅は玉の通る幅
TEST(SlamArrow, RunsFromTheBallEdgeAsWideAsTheBall)
{
    LevelNs::SlamArrowShape shape{};
    ASSERT_TRUE(LevelNs::BuildSlamArrow(GrownStateAhead(0.2f), LevelNs::SlamArrowDesc{}, shape));

    ExpectVector3Near(shape.origin, Vector3{0.0f, 1.15f, 0.0f});
    ExpectVector3Near(shape.direction, Vector3{1.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(shape.start, k_BallRadius);
    EXPECT_FLOAT_EQ(shape.bandWidth, 2.0f * k_BallRadius);
    EXPECT_FLOAT_EQ(shape.fullTip, 10.0f);
    EXPECT_FLOAT_EQ(shape.tip, 10.0f);
}

// 狙う相手のいる線では、矢印の先は相手の手前の面 (掃いた玉が相手の当たりの形に触れる所 + 玉の半径)
// 狙う相手の予測の along は相手の中心までのまま
TEST(SlamArrow, TipStopsAtTheNearFaceOfTheTarget)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{});
    ASSERT_NE(rig.arrow, nullptr);
    ASSERT_NE(rig.input, nullptr);
    SettleAndAimAhead(scene, rig);

    MouseLeftPress press;
    StepUntilCharging(scene, rig);
    for (int i = 0; i < 10; ++i)
    {
        Step(scene, rig);
    }

    LevelNs::SlamLineTarget aim{};
    ASSERT_TRUE(rig.input->TryGetAimTarget(aim));
    const float radius = rig.movement->CapsuleRadius();
    // 1 m 立方の的の中心は x = 5。線の向きの半分の奥行きは 0.5
    EXPECT_NEAR(aim.origin.x + aim.along, 5.0f, 1e-4f);
    // 触れる所は 1 mm の幅まで詰めた値
    EXPECT_NEAR(aim.contact, aim.along - 0.5f - radius, 1e-3f);

    LevelNs::SlamArrowShape shape{};
    ASSERT_TRUE(rig.arrow->TryGetShownArrow(shape));
    EXPECT_FLOAT_EQ(shape.tip, shape.fullTip);
    // 的の手前の面は x = 4.5。触れる所を 1 mm の幅まで詰めるので、先も同じ幅で合う
    EXPECT_NEAR(shape.origin.x + shape.direction.x * shape.tip, 4.5f, 1e-3f);
}

// 矢じりの奥行きは、玉の中心から先までの距離の 0.28 倍を 1.3〜2.8 m に抑えた値
TEST(SlamArrow, HeadDepthIsAShareOfTheTipDistanceWithinLimits)
{
    const float contacts[] = {2.35f, 6.35f, 20.0f};
    const float depths[] = {1.3f, 0.28f * 7.0f, 2.8f};
    for (int i = 0; i < 3; ++i)
    {
        LevelNs::SlamArrowState state = GrownStateAhead(0.2f);
        state.targetContact = contacts[i];
        LevelNs::SlamArrowShape shape{};
        ASSERT_TRUE(LevelNs::BuildSlamArrow(state, LevelNs::SlamArrowDesc{}, shape)) << i;
        EXPECT_NEAR(shape.tip, contacts[i] + k_BallRadius, 1e-5f) << i;
        EXPECT_NEAR(shape.headDepth, depths[i], 1e-5f) << i;
    }
}

namespace
{
    // 矢印が出たフレームを 0 とした 0〜11 フレーム目に、伸びきった長さの 1/10 ずつ等速に伸び、10 フレーム目に伸びきる
    void ExpectGrowsOverTenFrames(SceneNs::Scene& scene, const MarkerRig& rig)
    {
        for (int k = 0; k < 12; ++k)
        {
            LevelNs::SlamArrowShape shape{};
            ASSERT_TRUE(rig.arrow->TryGetShownArrow(shape)) << k;
            const float grown = std::min(1.0f, static_cast<float>(k + 1) / 10.0f);
            EXPECT_NEAR(shape.tip, shape.start + (shape.fullTip - shape.start) * grown, 1e-4f) << k;
            Step(scene, rig);
        }
    }
} // namespace

// 矢印が出たフレームに伸びきった長さの 1/10 で出て、10 フレーム目に伸びきる。等速に伸びる
// 溜めに入った時に正面に相手がいれば溜めに入ったフレームから。カメラを外して戻すと、戻したフレームから伸び直す
TEST(SlamArrow, GrowsOverTenFramesFromTheFrameItAppears)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{});
    ASSERT_NE(rig.arrow, nullptr);
    ASSERT_NE(rig.input, nullptr);
    SettleAndAimAhead(scene, rig);

    MouseLeftPress press;
    StepUntilCharging(scene, rig);
    ExpectGrowsOverTenFrames(scene, rig);

    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{0.0f, 0.0f, 1.0f}));
    Step(scene, rig);
    LevelNs::SlamArrowShape away{};
    ASSERT_FALSE(rig.arrow->TryGetShownArrow(away));

    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    Step(scene, rig);
    ASSERT_TRUE(rig.input->IsCharging());
    ExpectGrowsOverTenFrames(scene, rig);
}

// 溜めている間にスティックを横と後ろへ倒しても、狙いの線と矢印はカメラの正面のまま、狙う相手と枠も正面の相手のまま
TEST(SlamArrow, StaysOnTheCameraFrontWhateverTheStick)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{});
    ASSERT_NE(rig.arrow, nullptr);
    ASSERT_NE(rig.input, nullptr);
    ASSERT_NE(rig.target, nullptr);
    SettleAndAimAhead(scene, rig);

    MouseLeftPress press;
    StepUntilCharging(scene, rig);
    const Vector3 sticks[] = {Vector3{0.0f, 0.0f, 1.0f}, Vector3{0.0f, 0.0f, -1.0f}, Vector3{-1.0f, 0.0f, 0.0f}};
    for (const Vector3& stick : sticks)
    {
        rig.movement->SetDesiredMove(stick, 0.0f);
        Step(scene, rig);
        ASSERT_TRUE(rig.input->IsCharging());
        LevelNs::AimLine line{};
        ASSERT_TRUE(rig.input->TryGetAimLine(line)) << stick.z;
        ExpectVector3Near(line.direction, Vector3{1.0f, 0.0f, 0.0f});
        LevelNs::SlamLineTarget aim{};
        ASSERT_TRUE(rig.input->TryGetAimTarget(aim)) << stick.z;
        EXPECT_EQ(aim.target, SceneNs::ObjectRef{rig.target->Id()});
        LevelNs::SlamArrowShape shape{};
        ASSERT_TRUE(rig.arrow->TryGetShownArrow(shape)) << stick.z;
        ExpectVector3Near(shape.direction, Vector3{1.0f, 0.0f, 0.0f});
        EXPECT_EQ(rig.marker->ShownTargetRef(), SceneNs::ObjectRef{rig.target->Id()});
    }
}

// カメラの正面に相手がいなければ、スティックを相手へ倒していても矢印を組まず、枠も出さない
TEST(SlamArrow, BuildsNothingWithoutATargetInFrontOfTheCamera)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{});
    ASSERT_NE(rig.arrow, nullptr);
    ASSERT_NE(rig.input, nullptr);
    SettleAndAimAhead(scene, rig);
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{0.0f, 0.0f, 1.0f}));

    MouseLeftPress press;
    StepUntilCharging(scene, rig);
    for (int i = 0; i < 12; ++i)
    {
        Step(scene, rig);
        ASSERT_TRUE(rig.input->IsCharging());
        LevelNs::SlamLineTarget aim{};
        EXPECT_FALSE(rig.input->TryGetAimTarget(aim)) << i;
        LevelNs::SlamArrowShape shape{};
        EXPECT_FALSE(rig.arrow->TryGetShownArrow(shape)) << i;
        EXPECT_FALSE(rig.marker->ShownTargetRef().IsSet()) << i;
        LevelNs::LockOnFrameShape frame{};
        EXPECT_FALSE(rig.marker->BuildShownShape(ChaseView(), k_TargetSize, frame)) << i;
    }
}

// 矢印は溜めている間だけ組む。押しているが溜めに入る前と、放したフレームは組まない
TEST(SlamArrow, ShowsTheArrowOnlyWhileCharging)
{
    SceneNs::Scene scene;
    MarkerRig rig = BuildMarkerCourse(scene, MarkerCourse{});
    ASSERT_NE(rig.arrow, nullptr);
    ASSERT_NE(rig.input, nullptr);
    SettleAndAimAhead(scene, rig);
    LevelNs::SlamArrowShape shape{};

    MouseLeftPress press;
    Step(scene, rig);
    ASSERT_TRUE(rig.input->Judge().IsHeld());
    ASSERT_FALSE(rig.input->IsCharging());
    EXPECT_FALSE(rig.arrow->TryGetShownArrow(shape));

    StepUntilCharging(scene, rig);
    for (int i = 0; i < 20; ++i)
    {
        Step(scene, rig);
    }
    EXPECT_TRUE(rig.arrow->TryGetShownArrow(shape));

    press.Release();
    Step(scene, rig);
    ASSERT_FALSE(rig.input->IsCharging());
    EXPECT_FALSE(rig.arrow->TryGetShownArrow(shape));
}

// 色の付いた部分は玉の縁から (届く所 − 玉の縁) × 溜め量まで。色は溜め 1/3 未満で前半の色、
// 1/3 から溜めきりの前まで後半の色、溜めきりで溜めきりの色になり、矢じりの先まで全部に付く
TEST(SlamArrow, ColoredPartGrowsWithChargeInThreeStages)
{
    const LevelNs::SlamArrowDesc desc{};
    struct Case
    {
        float charge01;
        bool full;
        Vector3 color;
    };
    const Case cases[] = {
        {0.0f, false, desc.earlyColor},
        {0.2f, false, desc.earlyColor},
        {1.0f / 3.0f, false, desc.lateColor},
        {0.9f, false, desc.lateColor},
        {1.0f, true, desc.fullColor},
    };
    for (const Case& c : cases)
    {
        LevelNs::SlamArrowState state = GrownStateAhead(c.charge01);
        state.chargeFull = c.full;
        LevelNs::SlamArrowShape shape{};
        ASSERT_TRUE(LevelNs::BuildSlamArrow(state, desc, shape)) << c.charge01;
        EXPECT_NEAR(shape.colorFront, k_BallRadius + (10.0f - k_BallRadius) * c.charge01, 1e-4f) << c.charge01;
        EXPECT_EQ(shape.fullyColored, c.full) << c.charge01;
        ExpectVector3Near(shape.stageColor, c.color);
    }
    // 前半と後半の色は緑と黄、溜めきりの色は赤
    ExpectVector3Near(desc.earlyColor, Vector3{72.0f / 255.0f, 230.0f / 255.0f, 120.0f / 255.0f});
    ExpectVector3Near(desc.lateColor, Vector3{1.0f, 208.0f / 255.0f, 48.0f / 255.0f});
    ExpectVector3Near(desc.fullColor, Vector3{1.0f, 64.0f / 255.0f, 56.0f / 255.0f});
}

// 床の無い所には帯を置かない。同じ高さで続く区切りは 1 枚につなぎ、床の上 3 cm に置く
// 玉の下の床より玉の半径より深い所の床 (落下死の体積の上面など) には置かない
TEST(SlamArrow, LeavesOutPiecesWithoutAFloorBelow)
{
    LevelNs::SlamArrowShape built{};
    ASSERT_TRUE(LevelNs::BuildSlamArrow(GrownStateAhead(0.2f), LevelNs::SlamArrowDesc{}, built));

    // x が 4〜5 の間は穴で、その下 10 m に深い床がある。それ以外は高さ 0.5 の床
    const LevelNs::SlamArrowGroundProbe holeProbe = [](const Vector3& from, float maxDepth, float& outGroundY) {
        float ground = 0.5f;
        if (from.x > 4.0f && from.x < 5.0f)
        {
            ground = -10.0f;
        }
        if (from.y - ground > maxDepth)
        {
            return false;
        }
        outGroundY = ground;
        return true;
    };
    LevelNs::SlamArrowShape shape = built;
    LevelNs::PlaceSlamArrowOnGround(holeProbe, 0.03f, shape);
    ASSERT_EQ(shape.band.size(), 2u);
    EXPECT_NEAR(shape.band[0].alongNear, k_BallRadius, 1e-4f);
    EXPECT_NEAR(shape.band[0].alongFar, 4.0f, 0.1f);
    EXPECT_NEAR(shape.band[1].alongNear, 5.0f, 0.1f);
    EXPECT_NEAR(shape.band[1].alongFar, 10.0f, 1e-4f);
    for (const LevelNs::SlamArrowPiece& piece : shape.band)
    {
        EXPECT_NEAR(piece.height, 0.53f, 1e-5f);
        // 穴の上に帯を置かない
        EXPECT_FALSE(piece.alongFar > 4.06f && piece.alongNear < 4.94f);
    }
    ASSERT_TRUE(shape.hasHead);
    EXPECT_NEAR(shape.head.height, 0.53f, 1e-5f);

    // 床が x = 3 で終わる線では、帯は床の端で切れ、矢じりは置かない
    const LevelNs::SlamArrowGroundProbe ledgeProbe = [](const Vector3& from, float maxDepth, float& outGroundY) {
        float ground = 0.5f;
        if (from.x > 3.0f)
        {
            ground = -10.0f;
        }
        if (from.y - ground > maxDepth)
        {
            return false;
        }
        outGroundY = ground;
        return true;
    };
    LevelNs::SlamArrowShape ledge = built;
    LevelNs::PlaceSlamArrowOnGround(ledgeProbe, 0.03f, ledge);
    ASSERT_EQ(ledge.band.size(), 1u);
    EXPECT_NEAR(ledge.band[0].alongFar, 3.0f, 0.1f);
    EXPECT_FALSE(ledge.hasHead);
}

// 欄の壊れた値と、壊れた様子では組まずに偽を返し、結果を書き換えない
TEST(SlamArrow, BrokenValuesBuildNothing)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const LevelNs::SlamArrowDesc brokenDescs[] = {
        LevelNs::SlamArrowDesc{.growFrames = 0},
        LevelNs::SlamArrowDesc{.headWidth = 0.0f},
        LevelNs::SlamArrowDesc{.headDepthMin = -1.0f},
        LevelNs::SlamArrowDesc{.headDepthMax = nan},
        LevelNs::SlamArrowDesc{.startFade = 0.0f},
        LevelNs::SlamArrowDesc{.frontSoftness = nan},
        LevelNs::SlamArrowDesc{.bandFillAlpha = nan},
    };
    for (const LevelNs::SlamArrowDesc& desc : brokenDescs)
    {
        LevelNs::SlamArrowShape shape{.tip = 123.0f};
        EXPECT_FALSE(LevelNs::BuildSlamArrow(GrownStateAhead(0.2f), desc, shape));
        EXPECT_FLOAT_EQ(shape.tip, 123.0f);
    }

    LevelNs::SlamArrowState noDirection = GrownStateAhead(0.2f);
    noDirection.line.direction = Vector3{0.0f, 0.0f, 0.0f};
    LevelNs::SlamArrowState negativeFrames = GrownStateAhead(0.2f);
    negativeFrames.framesSinceShown = -1;
    LevelNs::SlamArrowState nanContact = GrownStateAhead(0.2f);
    nanContact.targetContact = nan;
    for (const LevelNs::SlamArrowState& state : {noDirection, negativeFrames, nanContact})
    {
        LevelNs::SlamArrowShape shape{.tip = 123.0f};
        EXPECT_FALSE(LevelNs::BuildSlamArrow(state, LevelNs::SlamArrowDesc{}, shape));
        EXPECT_FLOAT_EQ(shape.tip, 123.0f);
    }
}

#include "Editor/EditorActors.h"
#include "Game/Level/AirGravity.h"
#include "Game/Level/HitTier.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamArrow.h"
#include "Game/Level/TargetMarker.h"
#include "Game/Player/PlayerParams.h"
#include "Game/PlayerActor.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ClassObjs/Body.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/Clock.h"
#include "Tests/TestViewCamera.h"
#include "TestActorFile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// 溜めている間の矢印が、放った玉の実際に通る道筋に高さ込みで沿うか (R-10)
// 放つ上下の速さを狙いの段で控えて放す時に添えるか (R-7・R-8)

namespace
{
    using NS::Vector3;

    // 床の上面 y = 0 に自機と、+z の先に相手を置く。カメラは +z を向く
    PlayerActor* PlaceOnFloor(NS::Scene& scene,
                              const Vector3& playerPosition,
                              bool withTarget,
                              float targetScale,
                              const Vector3& targetPosition,
                              const nlohmann::json& targetParts = nlohmann::json::object())
    {
        nlohmann::json doc = NS::MakeSceneJson();
        nlohmann::json player = NS::MakeActorJson();
        NS::SetActorJsonFile(player, "PlayerActor");
        NS::SetActorJsonId(player, 1);
        NS::Editor::SetActorJsonPosition(player, playerPosition);
        NS::SceneJsonActors(doc).push_back(std::move(player));
        if (withTarget)
        {
            nlohmann::json rock = NS::MakeActorJson();
            NS::SetActorJsonFile(rock, "PropActor");
            NS::SetActorJsonId(rock, 2);
            NS::Editor::SetActorJsonPosition(rock, targetPosition);
            NS::Editor::SetActorJsonScale(rock, Vector3{targetScale, targetScale, targetScale});
            for (nlohmann::json::const_iterator it = targetParts.begin(); it != targetParts.end(); ++it)
            {
                NS::ActorJsonClassObjs(rock)[it.key()] = it.value();
            }
            NS::SceneJsonActors(doc).push_back(std::move(rock));
        }
        scene.ResetFromJson(doc);
        NS::OBB floor{};
        floor.center = Vector3{0.0f, -0.5f, 0.0f};
        floor.halfExtentX = 100.0f;
        floor.halfExtentY = 0.5f;
        floor.halfExtentZ = 100.0f;
        scene.Physics().AddBox(floor, NS::Phys::ObjectLayers::Terrain);
        PlaceViewCamera(scene, Vector3{0.0f, playerPosition.y, -1.0f}, Vector3{0.0f, playerPosition.y, 1.0f});
        return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
    }

    // 線に沿った距離 along での帯の高さ。板の中は近い端から遠い端へ直線で上がる
    bool BandHeightAt(const GL::Level::SlamArrowShape& arrow, float along, float& outHeight)
    {
        for (const GL::Level::SlamArrowPiece& piece : arrow.band)
        {
            if (along < piece.alongNear || along > piece.alongFar)
            {
                continue;
            }
            const float t = (along - piece.alongNear) / (piece.alongFar - piece.alongNear);
            outHeight = piece.height + piece.rise * t;
            return true;
        }
        return false;
    }

    constexpr int k_ChargeFrames = 70;
} // namespace

// 矢印は世界を描いてにじませた後の重ね描きで描き、溜めの光とそのにじみが矢じりを白く覆わない。ロックオンの
// 枠は矢印より後に重ね、枠が矢印の下に隠れない
TEST(SlamArrowTest, ArrowIsOverlaidBeforeTheLockOnFrame)
{
    NS::Scene scene;
    PlayerActor* player = PlaceOnFloor(scene, Vector3{0.0f, 1.0f, 0.0f}, false, 1.0f, Vector3{});
    ASSERT_NE(player, nullptr);
    EXPECT_LT(player->SlamIndicator().OverlayOrder(), player->TargetIndicator().OverlayOrder());
}

// 溜めきりの後は、溜めすぎの深さで溜めきりの赤から溜めすぎの紫へ移る。赤のうちと溜めきる前は今の色のまま
TEST(SlamArrowTest, FullArrowTurnsFromRedToPurpleWithTheOvercharge)
{
    GL::Level::SlamArrowDesc desc{};
    GL::Level::SlamArrowState state{};
    state.line.origin = Vector3{};
    state.line.direction = Vector3{0.0f, 0.0f, 1.0f};
    state.line.length = 10.0f;
    state.ballRadius = 0.5f;
    state.charge01 = 1.0f;
    state.chargeFull = true;
    GL::Level::SlamArrowShape shape{};
    ASSERT_TRUE(GL::Level::BuildSlamArrow(state, desc, shape));
    EXPECT_TRUE(shape.stageColor == desc.fullColor.ToVector3());

    state.overcharge01 = 1.0f;
    ASSERT_TRUE(GL::Level::BuildSlamArrow(state, desc, shape));
    EXPECT_TRUE(shape.stageColor == desc.overchargeColor.ToVector3());

    state.overcharge01 = 0.5f;
    ASSERT_TRUE(GL::Level::BuildSlamArrow(state, desc, shape));
    EXPECT_FLOAT_EQ(shape.stageColor.x, (desc.fullColor.x + desc.overchargeColor.x) * 0.5f);
    EXPECT_FLOAT_EQ(shape.stageColor.y, (desc.fullColor.y + desc.overchargeColor.y) * 0.5f);
    EXPECT_FLOAT_EQ(shape.stageColor.z, (desc.fullColor.z + desc.overchargeColor.z) * 0.5f);

    // 溜めきる前に溜めすぎの深さが入っていても紫にしない
    state.chargeFull = false;
    state.charge01 = 0.9f;
    ASSERT_TRUE(GL::Level::BuildSlamArrow(state, desc, shape));
    EXPECT_TRUE(shape.stageColor == desc.lateColor.ToVector3());
}

namespace
{
    // 床 (y = 0) に貼った +z へ向かう矢印を、framesSinceShown フレーム目の長さで組んで描く単位にする
    std::vector<NS::Gfx::DrawItem> DrawGroundArrow(const GL::Level::SlamArrowDesc& desc,
                                                   int framesSinceShown,
                                                   const Vector3& cameraPosition,
                                                   GL::Level::SlamArrowShape& outShape)
    {
        GL::Level::SlamArrowState state{};
        state.line.origin = Vector3{0.0f, 0.5f, 0.0f};
        state.line.direction = Vector3{0.0f, 0.0f, 1.0f};
        state.line.length = 10.0f;
        state.line.grounded = true;
        state.ballRadius = 0.5f;
        state.charge01 = 0.1f;
        state.framesSinceShown = framesSinceShown;
        EXPECT_TRUE(GL::Level::BuildSlamArrow(state, desc, outShape));
        const GL::Level::SlamArrowGroundProbe flat = [](const Vector3&, float, float& outGroundY) {
            outGroundY = 0.0f;
            return true;
        };
        GL::Level::PlaceSlamArrowOnGround(flat, desc, outShape);
        EXPECT_TRUE(outShape.hasHead);
        std::vector<NS::Gfx::DrawItem> items;
        GL::Level::AppendSlamArrowDrawItems(
            outShape, desc, NS::Matrix::Identity, cameraPosition, GL::Level::SlamArrowDrawAssets{}, items);
        return items;
    }

    // 溜め始めの床の矢印を、出したフレームの短い長さのまま真上の高いカメラから描く
    std::vector<NS::Gfx::DrawItem> DrawEarlyGroundArrow(const GL::Level::SlamArrowDesc& desc)
    {
        GL::Level::SlamArrowShape shape{};
        return DrawGroundArrow(desc, 0, Vector3{0.0f, 40.0f, -6.0f}, shape);
    }

    GL::Level::SlamArrowConstants ConstantsOf(const NS::Gfx::DrawItem& item)
    {
        GL::Level::SlamArrowConstants constants{};
        std::memcpy(&constants, &item.constants, sizeof(constants));
        return constants;
    }
} // namespace

// 帯と矢じりの板は裏からも描く。下る道筋の板はカメラから裏を向き、裏を描かないと帯の先と矢じりが丸ごと消える
TEST(SlamArrowTest, EveryPlateIsDrawnFromBothSides)
{
    const std::vector<NS::Gfx::DrawItem> items = DrawEarlyGroundArrow(GL::Level::SlamArrowDesc{});
    ASSERT_GE(items.size(), 3u);
    for (const NS::Gfx::DrawItem& item : items)
    {
        EXPECT_TRUE(item.twoSided);
    }
}

TEST(SlamArrowTest, InstanceTextureSpanChangesTheActualPlateWidth)
{
    GL::Level::SlamArrowDesc desc;
    GL::Level::SlamArrowShape originalShape;
    const std::vector<NS::Gfx::DrawItem> original = DrawGroundArrow(desc, 10, {0.0f, 20.0f, -10.0f}, originalShape);
    ASSERT_FALSE(original.empty());
    desc.bandTextureSpan *= 0.5f;
    GL::Level::SlamArrowShape changedShape;
    const std::vector<NS::Gfx::DrawItem> changed = DrawGroundArrow(desc, 10, {0.0f, 20.0f, -10.0f}, changedShape);
    ASSERT_FALSE(changed.empty());
    EXPECT_FLOAT_EQ(ConstantsOf(changed.front()).world._11, ConstantsOf(original.front()).world._11 * 2.0f);
    EXPECT_FLOAT_EQ(changedShape.tip, originalShape.tip);
    desc.bandTextureSpan = 0.0f;
    std::vector<NS::Gfx::DrawItem> invalid;
    GL::Level::AppendSlamArrowDrawItems(changedShape, desc, NS::Matrix::Identity, {}, {}, invalid);
    EXPECT_TRUE(invalid.empty());
}

TEST(SlamArrowTest, InstanceGroundSamplingControlsProbesAndRejectsZeroSpacing)
{
    GL::Level::SlamArrowShape shape;
    shape.origin = {0.0f, 1.0f, 0.0f};
    shape.direction = {0.0f, 0.0f, 1.0f};
    shape.bandWidth = 1.0f;
    shape.start = 0.0f;
    shape.tip = 1.0f;
    shape.headDepth = 0.2f;
    GL::Level::SlamArrowDesc desc;
    desc.groundProbeSpacing = 0.5f;
    desc.ballGroundSearchDepth = 3.0f;
    int probes = 0;
    float firstDepth = 0.0f;
    const GL::Level::SlamArrowGroundProbe probe = [&](const Vector3&, float depth, float& ground) {
        if (probes == 0)
        {
            firstDepth = depth;
        }
        ++probes;
        ground = 0.0f;
        return true;
    };
    GL::Level::PlaceSlamArrowOnGround(probe, desc, shape);
    EXPECT_EQ(probes, 4);
    EXPECT_FLOAT_EQ(firstDepth, 3.0f);
    ASSERT_EQ(shape.band.size(), 1u);
    EXPECT_FLOAT_EQ(shape.band.front().height, desc.groundLift);
    desc.groundProbeSpacing = 0.0f;
    GL::Level::PlaceSlamArrowOnGround(probe, desc, shape);
    EXPECT_TRUE(shape.band.empty());
    EXPECT_FALSE(shape.hasHead);
    EXPECT_EQ(probes, 4);
}

// 矢じりは溜めの始めから段の色で塗り、始まりのぼかしも掛けない。白っぽい薄い塗りだと市松の床に溶け、
// 出たばかりの短い矢印では玉の縁のぼかしに矢じりまで消える。溜め量を示す色の先は帯だけが持つ
TEST(SlamArrowTest, HeadIsColoredAndUnfadedFromTheStartOfTheCharge)
{
    const std::vector<NS::Gfx::DrawItem> items = DrawEarlyGroundArrow(GL::Level::SlamArrowDesc{});
    ASSERT_GE(items.size(), 3u);
    // 後ろの 2 つが矢じりと、隠れた所へ描く矢じり
    for (std::size_t i = items.size() - 2; i < items.size(); ++i)
    {
        SCOPED_TRACE(i);
        const GL::Level::SlamArrowConstants head = ConstantsOf(items[i]);
        EXPECT_FLOAT_EQ(head.fadeAndFront.x, 1.0f);
        EXPECT_FLOAT_EQ(head.fadeAndFront.y, 0.0f);
        EXPECT_FLOAT_EQ(head.fadeAndFront.z, 1.0f);
        EXPECT_FLOAT_EQ(head.fadeAndFront.w, 0.0f);
    }
}

// カメラが低く、床の矢じりを見る角度が欄「矢じりを見せる最小の角度」を下回る時は、矢じりの板を手前の端を軸に
// カメラの方へ起こし、手前の端をその角度で見せる。起こしても板の長さは変えない。カメラが高ければ床に寝たまま
TEST(SlamArrowTest, LowCameraStandsTheHeadUpToTheMinimumViewAngle)
{
    const GL::Level::SlamArrowDesc desc{};
    const Vector3 high{0.0f, 40.0f, -6.0f};
    const Vector3 low{0.0f, 1.0f, -6.0f};
    GL::Level::SlamArrowShape shape{};
    const std::vector<NS::Gfx::DrawItem> lying = DrawGroundArrow(desc, 30, high, shape);
    const std::vector<NS::Gfx::DrawItem> standing = DrawGroundArrow(desc, 30, low, shape);
    ASSERT_GE(lying.size(), 3u);
    ASSERT_EQ(lying.size(), standing.size());
    // 起こす軸の、矢じりの手前の端
    const Vector3 pivot =
        shape.origin + shape.direction * shape.head.alongNear + Vector3{0.0f, shape.head.height - shape.origin.y, 0.0f};
    for (std::size_t i = lying.size() - 2; i < lying.size(); ++i)
    {
        SCOPED_TRACE(i);
        const NS::Matrix flat = ConstantsOf(lying[i]).world;
        const NS::Matrix raised = ConstantsOf(standing[i]).world;
        EXPECT_FLOAT_EQ(flat._32, 0.0f);
        const Vector3 lengthAxis{raised._31, raised._32, raised._33};
        const Vector3 widthAxis{raised._11, raised._12, raised._13};
        const Vector3 lyingLengthAxis{flat._31, flat._32, flat._33};
        EXPECT_NEAR(lengthAxis.Length(), lyingLengthAxis.Length(), 1.0e-4f);
        Vector3 normal = lengthAxis.Cross(widthAxis);
        normal.Normalize();
        Vector3 toCamera = low - pivot;
        toCamera.Normalize();
        const float viewDegrees = NS::RadiansToDegrees(std::asin(std::abs(normal.Dot(toCamera))));
        EXPECT_NEAR(viewDegrees, desc.headMinViewDegrees, 0.1f);
    }
    // 帯は床に寝たまま
    EXPECT_FLOAT_EQ(ConstantsOf(standing.front()).world._32, 0.0f);
}

#include "NSlib/Core/Interpolated.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"

#include <gtest/gtest.h>

TEST(Interpolated, SetWritesOnlyTheCurrentValueAndSnapshotCatchesUp)
{
    NS::Interpolated<NS::Vector3> value{NS::Vector3{1.0f, 0.0f, 0.0f}};
    value.Set(NS::Vector3{3.0f, 0.0f, 0.0f});

    EXPECT_FLOAT_EQ(value.Previous().x, 1.0f);
    EXPECT_FLOAT_EQ(value.Current().x, 3.0f);
    EXPECT_FLOAT_EQ(value.Lerp(0.5f).x, 2.0f);

    value.Snapshot();
    EXPECT_FLOAT_EQ(value.Previous().x, 3.0f);
}

TEST(Interpolated, TeleportWritesBothValues)
{
    NS::Interpolated<NS::Vector3> value{NS::Vector3{1.0f, 0.0f, 0.0f}};
    value.Teleport(NS::Vector3{5.0f, 0.0f, 0.0f});

    EXPECT_FLOAT_EQ(value.Previous().x, 5.0f);
    EXPECT_FLOAT_EQ(value.Lerp(0.0f).x, 5.0f);
}

TEST(Interpolated, AlphaOfOneReturnsTheCurrentValueExactly)
{
    // 前 + (今 − 前) は丸めで今からずれる。止めた世界を描く割合 1 の絵が、前の値の違いで変わらないようにする
    NS::Interpolated<NS::Vector3> value{NS::Vector3{0.1f, 0.0f, 0.0f}};
    value.Set(NS::Vector3{0.7f, 0.0f, 0.0f});

    EXPECT_EQ(value.Lerp(1.0f).x, 0.7f);
}

TEST(Interpolated, QuaternionsTurnHalfWayAtHalfAlpha)
{
    NS::Interpolated<NS::Quaternion> value{NS::Quaternion::Identity};
    value.Set(NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, NS::k_Pi * 0.5f));

    const NS::Quaternion halfway = NS::Quaternion::CreateFromAxisAngle(NS::Vector3::UnitY, NS::k_Pi * 0.25f);
    const NS::Quaternion shown = value.Lerp(0.5f);
    EXPECT_NEAR(shown.y, halfway.y, 1e-5f);
    EXPECT_NEAR(shown.w, halfway.w, 1e-5f);
}

TEST(TransformTeleport, MovesThePreviousPositionToo)
{
    // やり直しで出現位置へ戻した直後の 1 歩が、元の位置から補間して描かれないようにする
    NS::Transform root;
    root.SetPosition(NS::Vector3{10.0f, 0.0f, 0.0f});
    root.Snapshot();

    root.Teleport(NS::Vector3{2.0f, 0.0f, 0.0f});

    EXPECT_FLOAT_EQ(root.PreviousPosition().x, 2.0f);
    EXPECT_FLOAT_EQ(root.InterpolatedWorldMatrix(0.0f).Translation().x, 2.0f);
}

TEST(ModelInterpolation, HalfAlphaDrawsTheHalfWayScale)
{
    NS::Model model;
    ASSERT_TRUE(model.SetDrawScale(NS::Vector3{3.0f, 3.0f, 3.0f}));

    // 前の倍率は 1 のまま。真ん中は 2
    EXPECT_FLOAT_EQ(model.DrawWorldMatrix(NS::Matrix::Identity, 0.5f)._11, 2.0f);
}

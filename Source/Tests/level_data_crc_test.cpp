#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include "Framework/Math/Math.h"

#include <gtest/gtest.h>

namespace LevelNs = NS::Game::Level;

TEST(LevelDataCrcTest, EmptyLevelIsDeterministic)
{
    LevelNs::LevelData a, b;
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, DifferentKindsProduceDifferentCrc)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeGridObject(1, 2, 3, 100, 0));
    b.objects.push_back(LevelNs::MakeGridObject(1, 2, 3, 101, 0));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, PlayStateMutationDoesNotAffectLevelDataCrc)
{
    LevelNs::LevelData level;
    level.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    level.spawnX = 5;
    const auto before = level.ComputeCrc32();

    LevelNs::PlayState play;
    for (int i = 0; i < 100; ++i)
    {
        play.playerPosition.x += 0.1f;
        play.coinCount += 1;
        play.paused = !play.paused;
    }
    const auto after = level.ComputeCrc32();
    EXPECT_EQ(before, after);
}

TEST(LevelDataCrcTest, ObjectsSizeIsHashed)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    a.objects.push_back(LevelNs::MakeGridObject(1, 0, 0, 1, 0));
    b.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, RotationStepIsHashed)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    b.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 1));
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, MaterialPathsAreHashed)
{
    LevelNs::LevelData a, b;
    a.materialPaths.push_back("Assets/Materials/Stone.mat");
    b.materialPaths.push_back("Assets/Materials/Grass.mat");
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, MetadataFieldsAreHashed)
{
    LevelNs::LevelData a, b;
    a.themeId = 1;
    b.themeId = 2;
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, CameraVolumesAreHashed)
{
    LevelNs::LevelData a, b;
    LevelNs::CameraVolume cam{};
    cam.cameraPositionX = 5.0f;
    a.cameraVolumes.push_back(cam);
    cam.cameraPositionX = 9.0f;
    b.cameraVolumes.push_back(cam);
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, CameraVolumeCountIsHashed)
{
    LevelNs::LevelData a, b;
    a.cameraVolumes.push_back(LevelNs::CameraVolume{});
    a.cameraVolumes.push_back(LevelNs::CameraVolume{});
    b.cameraVolumes.push_back(LevelNs::CameraVolume{});
    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataCrcTest, VectorCapacityDoesNotAffectCrc)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    b.objects.reserve(1000);
    b.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));
    EXPECT_EQ(a.ComputeCrc32(), b.ComputeCrc32());
}

TEST(LevelDataComponents, ComponentDataHoldsTypeNameAndFields)
{
    LevelNs::ComponentData cd;
    cd.typeName = "BoxColliderComponent";
    cd.fields.push_back(LevelNs::FieldValue{"Half Extents", NS::Math::Vector3{0.5f, 0.5f, 0.5f}});
    cd.fields.push_back(LevelNs::FieldValue{"Radius", 1.0f});

    EXPECT_EQ(cd.typeName, "BoxColliderComponent");
    ASSERT_EQ(cd.fields.size(), 2u);
    EXPECT_EQ(cd.fields[0].name, "Half Extents");
    EXPECT_EQ(cd.fields[1].name, "Radius");
}

TEST(LevelDataComponents, ObjectInstanceCopyIsDeep)
{
    LevelNs::ObjectInstance a{};
    a.components.push_back(LevelNs::ComponentData{"HazardComponent", {}});

    LevelNs::ObjectInstance b = a;
    b.components.clear();

    EXPECT_EQ(a.components.size(), 1u);
    EXPECT_EQ(b.components.size(), 0u);
}

TEST(LevelDataComponents, Crc32ChangesWhenComponentAdded)
{
    LevelNs::LevelData a, b;
    a.objects.push_back(LevelNs::MakeGridObject(0, 0, 0, 1, 0));

    LevelNs::ObjectInstance withComponent = LevelNs::MakeGridObject(0, 0, 0, 1, 0);
    withComponent.components.push_back(LevelNs::ComponentData{"HazardComponent", {}});
    b.objects.push_back(withComponent);

    EXPECT_NE(a.ComputeCrc32(), b.ComputeCrc32());
}

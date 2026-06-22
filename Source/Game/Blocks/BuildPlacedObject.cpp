#include "Game/Blocks/BuildPlacedObject.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Scene/AssetManager.h"
#include "Framework/Scene/Components/CapsuleColliderComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/SphereColliderComponent.h"
#include "Game/Block.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Blocks/DecorationBlock.h"
#include "Game/Blocks/HazardBlock.h"
#include "Game/Blocks/PoleBlock.h"
#include "Game/Blocks/SlopeBlock.h"
#include "Game/Blocks/WaterBlock.h"
#include "Game/Level/LevelData.h"

namespace NS::Game::Blocks
{
    namespace
    {
        // 1m grid セルの半径。 grid 配置物の当たり箱と wedge 半サイズに使う
        constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

        // 掴み判定が現挙動を保つためのポール寸法。 PoleComponent 既定 (0.15 / 2.0) と高さが異なる
        constexpr float kPoleRadius = 0.15f;
        constexpr float kPoleHeight = 1.0f;
    } // namespace

    std::unique_ptr<NS::Scene::GameObject> BuildPlacedObject(const NS::Game::Level::ObjectInstance& object,
                                                             NS::Scene::AssetManager& assets,
                                                             const std::vector<std::string>& materialPaths)
    {
        using namespace NS::Game::Level;

        const bool gridAligned = (object.flags & kObjectFlagGridAligned) != 0;
        const auto color = GetBaseColor(object.kind);
        const NS::Math::Vector3 baseColor{color.R(), color.G(), color.B()};

        NS::Graphics::StaticMesh* const cubeMesh = assets.Builtin("cube");
        NS::Graphics::Material* const blockMat = assets.SharedMaterial("block");

        std::unique_ptr<NS::Scene::GameObject> result;

        if (!gridAligned)
        {
            // free 配置物は materialIndex から .mat を解決する。 無効 / 失敗時は共有 player material へ倒す
            NS::Graphics::Material* freeMat = assets.SharedMaterial("player");
            if (object.materialIndex >= 0 && static_cast<std::size_t>(object.materialIndex) < materialPaths.size())
            {
                const auto loaded =
                    assets.LoadMaterial(NS::Core::FileSystem::ContentRoot() / materialPaths[object.materialIndex]);
                if (loaded.material != nullptr)
                    freeMat = loaded.material;
            }

            const NS::Math::Vector3 colliderHalfExtents{
                object.colliderHalfExtentsX, object.colliderHalfExtentsY, object.colliderHalfExtentsZ};
            const NS::Math::Vector3 colliderOffset{
                object.colliderOffsetX, object.colliderOffsetY, object.colliderOffsetZ};
            const NS::Math::Quaternion colliderRotation{
                object.colliderRotationX, object.colliderRotationY, object.colliderRotationZ, object.colliderRotationW};

            auto cube = std::make_unique<Block>(cubeMesh, freeMat, colliderHalfExtents);
            cube->Collider().SetCenterOffset(colliderOffset);
            cube->Collider().SetLocalRotation(colliderRotation);

            // 形状別の当たりを内蔵 Box に加えて足す。 視覚は cube のまま、 内蔵 Box は当たり退避として残す
            const ShapeCollider shape = ObjectShapeCollider(object);
            if (shape == ShapeCollider::Sphere)
            {
                auto* sphere = cube->AddComponent<NS::Scene::SphereColliderComponent>(object.colliderHalfExtentsX);
                sphere->SetCenterOffset(colliderOffset);
            }
            else if (shape == ShapeCollider::Capsule)
            {
                auto* capsule = cube->AddComponent<NS::Scene::CapsuleColliderComponent>(object.colliderHalfExtentsX,
                                                                                        object.colliderHalfExtentsY);
                capsule->SetCenterOffset(colliderOffset);
                capsule->SetLocalRotation(colliderRotation);
            }

            result = std::move(cube);
        }
        else if (object.kind == kBlockIdSolid)
        {
            result = std::make_unique<Block>(cubeMesh, blockMat, kCellHalfExtents);
        }
        else if (IsSlopeBlock(object.kind))
        {
            NS::Graphics::StaticMesh* wedge = nullptr;
            if (object.kind == kBlockIdSlope45)
                wedge = assets.Builtin("wedge45");
            else if (object.kind == kBlockIdSlope30)
                wedge = assets.Builtin("wedge30");
            else if (object.kind == kBlockIdSlope22)
                wedge = assets.Builtin("wedge22");
            else if (object.kind == kBlockIdSlope15)
                wedge = assets.Builtin("wedge15");
            result = std::make_unique<SlopeBlock>(wedge, blockMat, GetSlopeAngleDegrees(object.kind), kCellHalfExtents);
        }
        else if (IsPoleBlock(object.kind))
        {
            result = std::make_unique<PoleBlock>(assets.Builtin("pole"), blockMat, kPoleRadius, kPoleHeight);
        }
        else if (IsHazardBlock(object.kind))
        {
            result = std::make_unique<HazardBlock>(cubeMesh, blockMat, kCellHalfExtents);
        }
        else if (IsWaterBlock(object.kind))
        {
            result = std::make_unique<WaterBlock>(cubeMesh, assets.SharedMaterial("water"));
        }
        else if (IsDecorationBlock(object.kind))
        {
            result = std::make_unique<DecorationBlock>(cubeMesh, blockMat);
        }
        else
        {
            // 未対応の grid kind (coin / star 等) は配置物として組まない。 呼出側が nullptr を読み飛ばす
            return nullptr;
        }

        result->Root().SetPosition(NS::Math::Vector3{object.positionX, object.positionY, object.positionZ});
        result->Root().SetRotation(
            NS::Math::Quaternion{object.rotationX, object.rotationY, object.rotationZ, object.rotationW});
        result->Root().SetScale(NS::Math::Vector3{object.scaleX, object.scaleY, object.scaleZ});

        if (auto* meshComp = FindComponent<NS::Scene::MeshRendererComponent>(*result))
            meshComp->SetBaseColor(baseColor);

        return result;
    }
} // namespace NS::Game::Blocks

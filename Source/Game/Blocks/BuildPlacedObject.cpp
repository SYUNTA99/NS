#include "Game/Blocks/BuildPlacedObject.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Scene/AssetManager.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CapsuleColliderComponent.h"
#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PoleComponent.h"
#include "Framework/Scene/Components/SlopeColliderComponent.h"
#include "Framework/Scene/Components/SphereColliderComponent.h"
#include "Framework/Scene/ReflectionJson.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/LevelJson.h"

#include <filesystem>
#include <optional>

namespace NS::Game::Blocks
{
    namespace
    {
        // 1m grid セルの半径。 grid 配置物の当たり箱と wedge 半サイズに使う
        constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

        // 掴み判定が現挙動を保つためのポール寸法。 PoleComponent 既定 (0.15 / 2.0) と高さが異なる
        constexpr float kPoleRadius = 0.15f;
        constexpr float kPoleHeight = 1.0f;

        // asset path を ContentRoot 配下へ正規化して返す。 .. で外へ出る path は nullopt にし任意ファイル読込を防ぐ
        std::optional<std::filesystem::path> ResolveContentPath(const std::string& relative)
        {
            namespace fs = std::filesystem;
            const fs::path root = NS::Core::FileSystem::ContentRoot().lexically_normal();
            fs::path resolved = (root / relative).lexically_normal();
            const fs::path rel = resolved.lexically_relative(root);
            if (rel.empty() || *rel.begin() == fs::path{".."})
                return std::nullopt;
            return resolved;
        }

        // free 配置物の material を解決する。 materialIndex 無効 / traversal は共有 player material に倒す
        NS::Graphics::Material* ResolveFreeMaterial(const NS::Game::Level::ObjectInstance& object,
                                                    NS::Scene::AssetManager& assets,
                                                    const std::vector<std::string>& materialPaths)
        {
            NS::Graphics::Material* material = assets.SharedMaterial("player");
            if (object.materialIndex >= 0 && static_cast<std::size_t>(object.materialIndex) < materialPaths.size())
            {
                if (const auto resolved = ResolveContentPath(materialPaths[object.materialIndex]))
                {
                    const auto loaded = assets.LoadMaterial(*resolved);
                    if (loaded.material != nullptr)
                        material = loaded.material;
                }
            }
            return material;
        }

        // kind から描画 geometry を引く。 mesh は反射で運べないので kind 駆動と components 駆動で共有する
        // free 配置物は形によらず cube、 grid は slope なら角度別 wedge・ pole なら pole・ それ以外は cube
        NS::Graphics::StaticMesh* ResolveVisualMesh(NS::Scene::AssetManager& assets,
                                                    const NS::Game::Level::ObjectInstance& object)
        {
            using namespace NS::Game::Level;
            if ((object.flags & kObjectFlagGridAligned) == 0)
                return assets.Builtin("cube");
            if (IsSlopeBlock(object.kind))
            {
                if (object.kind == kBlockIdSlope45)
                    return assets.Builtin("wedge45");
                if (object.kind == kBlockIdSlope30)
                    return assets.Builtin("wedge30");
                if (object.kind == kBlockIdSlope22)
                    return assets.Builtin("wedge22");
                if (object.kind == kBlockIdSlope15)
                    return assets.Builtin("wedge15");
                return assets.Builtin("cube");
            }
            if (IsPoleBlock(object.kind))
                return assets.Builtin("pole");
            return assets.Builtin("cube");
        }

        // full SSOT 主経路: object.components を ComponentRegistry で生成し反射 set で値を入れる
        std::unique_ptr<NS::Scene::GameObject> BuildFromComponents(const NS::Game::Level::ObjectInstance& object,
                                                                   NS::Scene::AssetManager& assets,
                                                                   const std::vector<std::string>& materialPaths)
        {
            auto obj = std::make_unique<NS::Scene::GameObject>();
            for (const auto& component : object.components)
            {
                NS::Scene::Component* created = NS::Scene::CreateComponent(component.typeName, *obj);
                if (created == nullptr)
                    continue; // allowlist 外 / 未知 type は読み飛ばす

                const nlohmann::json fields = NS::Game::Level::ComponentFieldsToJson(component);
                NS::Scene::ApplyJsonFields(*created, fields);

                // mesh / material は反射で運べない。 MeshRenderer には free material と kind 由来 geometry を当てる
                if (auto* mesh = dynamic_cast<NS::Scene::MeshRendererComponent*>(created))
                {
                    mesh->SetMaterial(ResolveFreeMaterial(object, assets, materialPaths));
                    mesh->SetMesh(ResolveVisualMesh(assets, object));
                }
            }
            return obj;
        }

        // 旧データ互換のフォールバック: components を持たない object を kind のレシピから組む
        // 未対応 grid kind (coin / star 等) は nullptr を返す
        std::unique_ptr<NS::Scene::GameObject> BuildFromKind(const NS::Game::Level::ObjectInstance& object,
                                                             NS::Scene::AssetManager& assets,
                                                             const std::vector<std::string>& materialPaths)
        {
            using namespace NS::Game::Level;

            const bool gridAligned = (object.flags & kObjectFlagGridAligned) != 0;
            NS::Graphics::StaticMesh* const visualMesh = ResolveVisualMesh(assets, object);
            NS::Graphics::Material* const blockMat = assets.SharedMaterial("block");

            auto obj = std::make_unique<NS::Scene::GameObject>();

            if (!gridAligned)
            {
                NS::Graphics::Material* freeMat = ResolveFreeMaterial(object, assets, materialPaths);

                const NS::Math::Vector3 colliderHalfExtents{
                    object.colliderHalfExtentsX, object.colliderHalfExtentsY, object.colliderHalfExtentsZ};
                const NS::Math::Vector3 colliderOffset{
                    object.colliderOffsetX, object.colliderOffsetY, object.colliderOffsetZ};
                const NS::Math::Quaternion colliderRotation{object.colliderRotationX,
                                                            object.colliderRotationY,
                                                            object.colliderRotationZ,
                                                            object.colliderRotationW};

                obj->AddComponent<NS::Scene::MeshRendererComponent>(visualMesh, freeMat);
                auto* box = obj->AddComponent<NS::Scene::BoxColliderComponent>(colliderHalfExtents);
                box->SetCenterOffset(colliderOffset);
                box->SetLocalRotation(colliderRotation);

                // 形状別の当たりを内蔵 Box に加えて足す。 視覚は cube のまま、 内蔵 Box は当たり退避として残す
                const ShapeCollider shape = ObjectShapeCollider(object);
                if (shape == ShapeCollider::Sphere)
                {
                    auto* sphere = obj->AddComponent<NS::Scene::SphereColliderComponent>(object.colliderHalfExtentsX);
                    sphere->SetCenterOffset(colliderOffset);
                }
                else if (shape == ShapeCollider::Capsule)
                {
                    auto* capsule = obj->AddComponent<NS::Scene::CapsuleColliderComponent>(object.colliderHalfExtentsX,
                                                                                           object.colliderHalfExtentsY);
                    capsule->SetCenterOffset(colliderOffset);
                    capsule->SetLocalRotation(colliderRotation);
                }
            }
            else if (object.kind == kBlockIdSolid)
            {
                obj->AddComponent<NS::Scene::MeshRendererComponent>(visualMesh, blockMat);
                obj->AddComponent<NS::Scene::BoxColliderComponent>(kCellHalfExtents);
            }
            else if (IsSlopeBlock(object.kind))
            {
                obj->AddComponent<NS::Scene::MeshRendererComponent>(visualMesh, blockMat);
                obj->AddComponent<NS::Scene::SlopeColliderComponent>(GetSlopeAngleDegrees(object.kind),
                                                                     kCellHalfExtents);
            }
            else if (IsPoleBlock(object.kind))
            {
                obj->AddComponent<NS::Scene::MeshRendererComponent>(visualMesh, blockMat);
                obj->AddComponent<NS::Scene::PoleComponent>(kPoleRadius, kPoleHeight);
            }
            else if (IsHazardBlock(object.kind))
            {
                obj->AddComponent<NS::Scene::MeshRendererComponent>(visualMesh, blockMat);
                obj->AddComponent<NS::Scene::BoxColliderComponent>(kCellHalfExtents);
                obj->AddComponent<NS::Scene::HazardComponent>();
            }
            else if (IsWaterBlock(object.kind))
            {
                obj->AddComponent<NS::Scene::MeshRendererComponent>(visualMesh, assets.SharedMaterial("water"));
            }
            else if (IsDecorationBlock(object.kind))
            {
                obj->AddComponent<NS::Scene::MeshRendererComponent>(visualMesh, blockMat);
            }
            else
            {
                // 未対応の grid kind (coin / star 等) は配置物として組まない。 呼出側が nullptr を読み飛ばす
                return nullptr;
            }
            return obj;
        }
    } // namespace

    std::unique_ptr<NS::Scene::GameObject> BuildPlacedObject(const NS::Game::Level::ObjectInstance& object,
                                                             NS::Scene::AssetManager& assets,
                                                             const std::vector<std::string>& materialPaths)
    {
        const auto color = GetBaseColor(object.kind);
        const NS::Math::Vector3 baseColor{color.R(), color.G(), color.B()};

        std::unique_ptr<NS::Scene::GameObject> obj;
        if (!object.components.empty())
        {
            obj = BuildFromComponents(object, assets, materialPaths);
        }
        else
        {
            obj = BuildFromKind(object, assets, materialPaths);
            if (obj == nullptr)
                return nullptr;
        }

        obj->Root().SetPosition(NS::Math::Vector3{object.positionX, object.positionY, object.positionZ});
        obj->Root().SetRotation(
            NS::Math::Quaternion{object.rotationX, object.rotationY, object.rotationZ, object.rotationW});
        obj->Root().SetScale(NS::Math::Vector3{object.scaleX, object.scaleY, object.scaleZ});

        if (auto* meshComp = FindComponent<NS::Scene::MeshRendererComponent>(*obj))
            meshComp->SetBaseColor(baseColor);

        return obj;
    }
} // namespace NS::Game::Blocks

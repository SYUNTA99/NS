#include "Game/Blocks/BuildPlacedObject.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Scene/AssetManager.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CapsuleColliderComponent.h"
#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PickupComponent.h"
#include "Framework/Scene/Components/PoleComponent.h"
#include "Framework/Scene/Components/SlopeColliderComponent.h"
#include "Framework/Scene/Components/SphereColliderComponent.h"
#include "Framework/Scene/ReflectionJson.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/LevelJson.h"

#include <filesystem>
#include <optional>
#include <variant>

namespace NS::Game::Blocks
{
    namespace
    {
        // 1m grid セルの半径。 grid 配置物の当たり箱と wedge 半サイズに使う
        constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

        // 掴み判定が現挙動を保つためのポール寸法。 PoleComponent 既定 (0.15 / 2.0) と高さが異なる
        constexpr float kPoleRadius = 0.15f;
        constexpr float kPoleHeight = 1.0f;

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

        // 共有 material 名 (player / block / water / shadow) なら true。 これ以外は .mat パス / 既定へ倒す
        bool IsSharedMaterialName(const std::string& ref) noexcept
        {
            return ref == "player" || ref == "block" || ref == "water" || ref == "shadow";
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

                // material 参照が共有名なら共有 material、 空なら materialIndex / 既定へ倒す。 mesh はメッシュ参照を
                // 参照優先で解決し、 空 / 解決不可なら kind 由来 geometry へフォールバックする
                if (auto* mesh = dynamic_cast<NS::Scene::MeshRendererComponent*>(created))
                {
                    const std::string& matRef = mesh->MaterialRef();
                    mesh->SetMaterial(IsSharedMaterialName(matRef)
                                          ? assets.SharedMaterial(matRef)
                                          : ResolveFreeMaterial(object, assets, materialPaths));
                    NS::Graphics::Mesh* resolved =
                        mesh->MeshRef().empty() ? nullptr : ResolveMeshFromRef(assets, mesh->MeshRef());
                    mesh->SetMesh(resolved != nullptr ? resolved : ResolveVisualMesh(assets, object));
                }
            }
            return obj;
        }

        // kind から描画メッシュの builtin 名を引く。 ResolveVisualMesh と同じ規則で名前だけを返す
        const char* VisualMeshName(std::uint16_t kind, bool gridAligned) noexcept
        {
            if (!gridAligned)
                return "cube";
            if (IsSlopeBlock(kind))
            {
                if (kind == kBlockIdSlope45)
                    return "wedge45";
                if (kind == kBlockIdSlope30)
                    return "wedge30";
                if (kind == kBlockIdSlope22)
                    return "wedge22";
                if (kind == kBlockIdSlope15)
                    return "wedge15";
                return "cube";
            }
            if (IsPoleBlock(kind))
                return "pole";
            return "cube";
        }

        // 当たり箱の quaternion を反射 "Rotation (deg)" が受ける Euler 度へ写す。 SetRotationEulerDegrees の逆変換
        NS::Math::Vector3 QuaternionToEulerDegrees(const NS::Math::Quaternion& q) noexcept
        {
            const NS::Math::Vector3 euler = q.ToEuler();
            return NS::Math::Vector3{NS::Math::RadiansToDegrees(euler.x),
                                     NS::Math::RadiansToDegrees(euler.y),
                                     NS::Math::RadiansToDegrees(euler.z)};
        }

        NS::Game::Level::ComponentData MakeComponentData(std::string typeName,
                                                         std::vector<NS::Game::Level::FieldValue> fields)
        {
            NS::Game::Level::ComponentData component;
            component.typeName = std::move(typeName);
            component.fields = std::move(fields);
            return component;
        }

        NS::Game::Level::ComponentData MeshRendererData(std::string meshName,
                                                        std::string materialName,
                                                        const NS::Math::Vector3& baseColor)
        {
            return MakeComponentData("MeshRendererComponent",
                                     {NS::Game::Level::FieldValue{"Mesh", std::move(meshName)},
                                      NS::Game::Level::FieldValue{"Material", std::move(materialName)},
                                      NS::Game::Level::FieldValue{"Base Color", baseColor}});
        }

        // 旧フォーマットの kind を読込時だけ持ち回る placeholder の型名。 LevelJson 側と綴りを合わせる契約
        constexpr const char* kLegacyKindTypeName = "LegacyKind";

        // object が LegacyKind placeholder を持てばその id を返す。 無ければ nullopt
        std::optional<std::uint16_t> FindLegacyKindId(const NS::Game::Level::ObjectInstance& object) noexcept
        {
            for (const auto& component : object.components)
            {
                if (component.typeName != kLegacyKindTypeName)
                    continue;
                for (const auto& field : component.fields)
                {
                    if (field.name == "id" && std::holds_alternative<int>(field.value))
                        return static_cast<std::uint16_t>(std::get<int>(field.value));
                }
            }
            return std::nullopt;
        }

        // object が指定 typeName の component を持つか
        bool HasComponentType(const NS::Game::Level::ObjectInstance& object, const char* typeName) noexcept
        {
            for (const auto& component : object.components)
                if (component.typeName == typeName)
                    return true;
            return false;
        }

        // PickupComponent の "Pickup Kind" を返す。 PickupComponent 無しは -1、 フィールド欠損は 0
        int PickupKindOf(const NS::Game::Level::ObjectInstance& object) noexcept
        {
            for (const auto& component : object.components)
            {
                if (component.typeName != "PickupComponent")
                    continue;
                for (const auto& field : component.fields)
                    if (field.name == "Pickup Kind" && std::holds_alternative<int>(field.value))
                        return std::get<int>(field.value);
                return 0;
            }
            return -1;
        }

        // SlopeColliderComponent の "Angle (deg)" を返す。 SlopeCollider 無しは -1
        float SlopeAngleOf(const NS::Game::Level::ObjectInstance& object) noexcept
        {
            for (const auto& component : object.components)
            {
                if (component.typeName != "SlopeColliderComponent")
                    continue;
                for (const auto& field : component.fields)
                    if (field.name == "Angle (deg)" && std::holds_alternative<float>(field.value))
                        return std::get<float>(field.value);
                return 0.0f;
            }
            return -1.0f;
        }
    } // namespace

    std::vector<NS::Game::Level::ComponentData> MaterializeLegacyKind(std::uint16_t kind,
                                                                      const NS::Game::Level::ObjectInstance& object)
    {
        using namespace NS::Game::Level;
        std::vector<ComponentData> result;

        const bool gridAligned = (object.flags & kObjectFlagGridAligned) != 0;

        // コイン / スターは視覚も当たりも持たず、 拾得の意味だけを PickupComponent で表す (不可視を維持)
        if (gridAligned && kind == kBlockIdCoin)
        {
            result.push_back(MakeComponentData("PickupComponent", {FieldValue{"Pickup Kind", 0}}));
            return result;
        }
        if (gridAligned && kind == kBlockIdPowerStar)
        {
            result.push_back(MakeComponentData("PickupComponent", {FieldValue{"Pickup Kind", 1}}));
            return result;
        }

        const NS::Math::Color color = GetBaseColor(kind);
        const NS::Math::Vector3 baseColor{color.R(), color.G(), color.B()};
        const std::string meshName = VisualMeshName(kind, gridAligned);

        if (!gridAligned)
        {
            // 自由配置物は cube + 当たり Box、 shape により Sphere / Capsule を退避追加する
            const NS::Math::Vector3 half{
                object.colliderHalfExtentsX, object.colliderHalfExtentsY, object.colliderHalfExtentsZ};
            const NS::Math::Vector3 offset{object.colliderOffsetX, object.colliderOffsetY, object.colliderOffsetZ};
            const NS::Math::Quaternion rotation{
                object.colliderRotationX, object.colliderRotationY, object.colliderRotationZ, object.colliderRotationW};
            const NS::Math::Vector3 rotationEuler = QuaternionToEulerDegrees(rotation);

            result.push_back(MeshRendererData(meshName, "", baseColor));
            result.push_back(MakeComponentData("BoxColliderComponent",
                                               {FieldValue{"Half Extents", half},
                                                FieldValue{"Center Offset", offset},
                                                FieldValue{"Rotation (deg)", rotationEuler}}));

            const ShapeCollider shape = ObjectShapeCollider(object);
            if (shape == ShapeCollider::Sphere)
            {
                result.push_back(MakeComponentData(
                    "SphereColliderComponent",
                    {FieldValue{"Radius", object.colliderHalfExtentsX}, FieldValue{"Center Offset", offset}}));
            }
            else if (shape == ShapeCollider::Capsule)
            {
                result.push_back(MakeComponentData("CapsuleColliderComponent",
                                                   {FieldValue{"Radius", object.colliderHalfExtentsX},
                                                    FieldValue{"Half Height", object.colliderHalfExtentsY},
                                                    FieldValue{"Center Offset", offset},
                                                    FieldValue{"Rotation (deg)", rotationEuler}}));
            }
            return result;
        }

        if (kind == kBlockIdSolid)
        {
            result.push_back(MeshRendererData(meshName, "block", baseColor));
            result.push_back(MakeComponentData("BoxColliderComponent", {FieldValue{"Half Extents", kCellHalfExtents}}));
        }
        else if (IsSlopeBlock(kind))
        {
            result.push_back(MeshRendererData(meshName, "block", baseColor));
            result.push_back(MakeComponentData(
                "SlopeColliderComponent",
                {FieldValue{"Angle (deg)", GetSlopeAngleDegrees(kind)}, FieldValue{"Half Extents", kCellHalfExtents}}));
        }
        else if (IsPoleBlock(kind))
        {
            result.push_back(MeshRendererData(meshName, "block", baseColor));
            result.push_back(MakeComponentData("PoleComponent",
                                               {FieldValue{"Radius", kPoleRadius}, FieldValue{"Height", kPoleHeight}}));
        }
        else if (IsHazardBlock(kind))
        {
            result.push_back(MeshRendererData(meshName, "block", baseColor));
            result.push_back(MakeComponentData("BoxColliderComponent", {FieldValue{"Half Extents", kCellHalfExtents}}));
            result.push_back(MakeComponentData("HazardComponent", {}));
        }
        else if (IsWaterBlock(kind))
        {
            result.push_back(MeshRendererData(meshName, "water", baseColor));
        }
        else if (IsDecorationBlock(kind))
        {
            result.push_back(MeshRendererData(meshName, "block", baseColor));
        }
        // 未対応の grid kind は空一覧のまま返す (呼出側が配置物として組まない)
        return result;
    }

    bool IsGridSolidObject(const NS::Game::Level::ObjectInstance& object)
    {
        using namespace NS::Game::Level;
        if ((object.flags & kObjectFlagGridAligned) == 0)
            return false;
        // 拾得 / slope / pole / hazard は固形でない。 残る BoxCollider 持ちだけが固形 block
        if (PickupKindOf(object) >= 0)
            return false;
        if (HasComponentType(object, "SlopeColliderComponent"))
            return false;
        if (HasComponentType(object, "PoleComponent"))
            return false;
        if (HasComponentType(object, "HazardComponent"))
            return false;
        return HasComponentType(object, "BoxColliderComponent");
    }

    bool IsRotatableObject(const NS::Game::Level::ObjectInstance& object)
    {
        // R で 90° 回す対象。 向きが意味を持つ slope と固形 block (掴み pole / 水 / 装飾は除く)
        return SlopeAngleOf(object) >= 0.0f || IsGridSolidObject(object);
    }

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

    NS::Graphics::Mesh* ResolveMeshFromRef(NS::Scene::AssetManager& assets, const std::string& meshRef)
    {
        if (meshRef.empty())
            return nullptr;
        // builtin 名を先引きする (cube / wedge45 / wedge30 / wedge22 / wedge15 / pole)
        if (NS::Graphics::StaticMesh* builtin = assets.Builtin(meshRef))
            return builtin;
        // builtin に無ければ ContentRoot 配下の相対パスとして glTF を読む。 .. の traversal は弾かれ nullptr
        const std::optional<std::filesystem::path> resolved = ResolveContentPath(meshRef);
        if (!resolved)
            return nullptr;
        return assets.GetOrLoadMesh(*resolved);
    }

    std::unique_ptr<NS::Scene::GameObject> BuildPlacedObject(const NS::Game::Level::ObjectInstance& object,
                                                             NS::Scene::AssetManager& assets,
                                                             const std::vector<std::string>& materialPaths)
    {
        std::unique_ptr<NS::Scene::GameObject> obj;
        if (!object.components.empty())
        {
            obj = BuildFromComponents(object, assets, materialPaths);
        }
        else
        {
            // components 空の旧データは kind を ComponentData へ展開してから単一 build 経路へ流す
            NS::Game::Level::ObjectInstance materialized = object;
            materialized.components = MaterializeLegacyKind(object.kind, object);
            if (materialized.components.empty())
                return nullptr; // 未対応 kind は配置物として組まない
            obj = BuildFromComponents(materialized, assets, materialPaths);
        }

        obj->Root().SetPosition(NS::Math::Vector3{object.positionX, object.positionY, object.positionZ});
        obj->Root().SetRotation(
            NS::Math::Quaternion{object.rotationX, object.rotationY, object.rotationZ, object.rotationW});
        obj->Root().SetScale(NS::Math::Vector3{object.scaleX, object.scaleY, object.scaleZ});

        return obj;
    }

    void MigrateLegacyLevel(NS::Game::Level::LevelData& level)
    {
        using namespace NS::Game::Level;
        for (ObjectInstance& object : level.objects)
        {
            // 読込時の旧 "kind" は LegacyKind placeholder に入る。 id を取り出して実 component へ展開し置換する
            // placeholder ごと差し替わるので migrate 後に LegacyKind は残らない
            if (const std::optional<std::uint16_t> legacyKind = FindLegacyKindId(object))
            {
                object.components = MaterializeLegacyKind(*legacyKind, object);
                continue;
            }

            // placeholder を持たず component が空で kind!=0 の object (seed / 旧 binary / kind だけ持つ JSON) も
            // kind を使って同様に展開する。 既に実 component を持つ object は触らない
            if (object.components.empty() && object.kind != 0)
                object.components = MaterializeLegacyKind(object.kind, object);
        }
    }
} // namespace NS::Game::Blocks

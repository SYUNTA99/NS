#include "GameCore/Blocks/BuildPlacedObject.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Scene/AssetManager.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CapsuleColliderComponent.h"
#include "Framework/Scene/Components/CharacterMovementComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PlayerInputComponent.h"
#include "Framework/Scene/Components/ShadowComponent.h"
#include "Framework/Scene/Components/SlopeColliderComponent.h"
#include "Framework/Scene/Components/SphereColliderComponent.h"
#include "Framework/Scene/Reflection.h"
#include "Framework/Scene/ReflectionJson.h"
#include "GameCore/Level/LevelData.h"
#include "GameCore/Level/LevelJson.h"
#include "GameCore/Player.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <variant>

namespace NS::GameCore::Blocks
{
    namespace
    {
        // 1m grid セルの半径。 cube の grid 当たり箱に使う
        constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

        // free 配置物の material を解決する。 materialIndex 無効 / traversal は共有 player material に倒す
        NS::Graphics::Material* ResolveFreeMaterial(const NS::GameCore::Level::ObjectInstance& object,
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

        // player / block / water / shadow の共有 material 名なら true。 これ以外は .mat パス / 既定へ倒す
        bool IsSharedMaterialName(const std::string& ref) noexcept
        {
            return ref == "player" || ref == "block" || ref == "water" || ref == "shadow";
        }

        // 器に既に載る同型 component を反射型名で探す。 適用済みの控えにある分は飛ばし、 無ければ nullptr
        NS::Scene::Component* FindExistingComponent(NS::Scene::GameObject& obj,
                                                    const std::string& typeName,
                                                    const std::vector<NS::Scene::Component*>& applied)
        {
            for (NS::Scene::Component* comp : obj.Components())
            {
                if (comp == nullptr)
                    continue;
                if (std::find(applied.begin(), applied.end(), comp) != applied.end())
                    continue;
                const NS::Scene::ReflectionInfo* info = comp->GetReflection();
                if (info != nullptr && typeName == info->typeName)
                    return comp;
            }
            return nullptr;
        }

        // full SSOT 主経路: object.components を器の既存同型へ適用し、 無い型は ComponentRegistry で生成する
        // 素の器では同型が無く全生成になり、 Player の器では ctor の既定構成へ値だけが写って二重生成しない
        // データと live は 1 対 1 で対応させ、 同型を重ねたデータは上書きせず重ねた数だけ立てる
        void ApplyComponentsFromData(NS::Scene::GameObject& obj,
                                     const NS::GameCore::Level::ObjectInstance& object,
                                     NS::Scene::AssetManager& assets,
                                     const std::vector<std::string>& materialPaths)
        {
            std::vector<NS::Scene::Component*> applied;
            for (const auto& component : object.components)
            {
                NS::Scene::Component* created = FindExistingComponent(obj, component.typeName, applied);
                if (created == nullptr)
                    created = NS::Scene::CreateComponent(component.typeName, obj);
                if (created == nullptr)
                    continue; // allowlist 外 / 未知 type は読み飛ばす
                applied.push_back(created);

                const nlohmann::json fields = NS::GameCore::Level::ComponentFieldsToJson(component);
                NS::Scene::ApplyJsonFields(*created, fields);

                // material 参照が共有名なら共有 material、 空なら materialIndex / 既定へ倒す。 mesh はメッシュ参照を
                // 参照優先で解決し、 空 / 解決不可なら cube へフォールバックする
                if (auto* mesh = NS::Scene::ComponentCast<NS::Scene::MeshRendererComponent>(created))
                {
                    const std::string& matRef = mesh->MaterialRef();
                    mesh->SetMaterial(IsSharedMaterialName(matRef)
                                          ? assets.SharedMaterial(matRef)
                                          : ResolveFreeMaterial(object, assets, materialPaths));
                    // メッシュ参照を解決し、 空 / 解決不可なら cube へ倒す。 移行済データは必ず参照を持つ
                    NS::Graphics::Mesh* resolved =
                        mesh->MeshRef().empty() ? nullptr : ResolveMeshFromRef(assets, mesh->MeshRef());
                    mesh->SetMesh(resolved != nullptr ? resolved : assets.Builtin("cube"));
                }
                // 接地影の共有資源はファクトリが賄う。 影は常に組み込み quad + 共有 shadow 材質で描く
                else if (auto* shadow = NS::Scene::ComponentCast<NS::Scene::ShadowComponent>(created))
                    shadow->SetResources(assets.Builtin("shadowQuad"), assets.SharedMaterial("shadow"));
            }
        }

        // 当たり箱の quaternion を反射 "Rotation (deg)" が受ける Euler 度へ写す。 SetRotationEulerDegrees の逆変換
        NS::Math::Vector3 QuaternionToEulerDegrees(const NS::Math::Quaternion& q) noexcept
        {
            const NS::Math::Vector3 euler = q.ToEuler();
            return NS::Math::Vector3{NS::Math::RadiansToDegrees(euler.x),
                                     NS::Math::RadiansToDegrees(euler.y),
                                     NS::Math::RadiansToDegrees(euler.z)};
        }

        NS::GameCore::Level::ComponentData MakeComponentData(std::string typeName,
                                                             std::vector<NS::GameCore::Level::FieldValue> fields)
        {
            NS::GameCore::Level::ComponentData component;
            component.typeName = std::move(typeName);
            component.fields = std::move(fields);
            return component;
        }

        NS::GameCore::Level::ComponentData MeshRendererData(std::string meshName,
                                                            std::string materialName,
                                                            const NS::Math::Vector3& baseColor)
        {
            return MakeComponentData("MeshRendererComponent",
                                     {NS::GameCore::Level::FieldValue{"Mesh", std::move(meshName)},
                                      NS::GameCore::Level::FieldValue{"Material", std::move(materialName)},
                                      NS::GameCore::Level::FieldValue{"Base Color", baseColor}});
        }

        // object が指定 typeName の component を持つか
        bool HasComponentType(const NS::GameCore::Level::ObjectInstance& object, const char* typeName) noexcept
        {
            return NS::GameCore::Level::FindComponentData(object, typeName) != nullptr;
        }

        // SlopeColliderComponent の "Angle (deg)" を返す。 SlopeCollider 無しは -1
        float SlopeAngleOf(const NS::GameCore::Level::ObjectInstance& object) noexcept
        {
            const NS::GameCore::Level::ComponentData* slope =
                NS::GameCore::Level::FindComponentData(object, "SlopeColliderComponent");
            if (slope == nullptr)
                return -1.0f;
            const NS::GameCore::Level::FieldValue* angle = NS::GameCore::Level::FindField(*slope, "Angle (deg)");
            if (angle != nullptr && std::holds_alternative<float>(angle->value))
                return std::get<float>(angle->value);
            return 0.0f;
        }

        // MeshRenderer の "Material" 参照を返す。 MeshRenderer / フィールド無しは nullptr
        const std::string* MaterialRefOf(const NS::GameCore::Level::ObjectInstance& object) noexcept
        {
            const NS::GameCore::Level::ComponentData* renderer =
                NS::GameCore::Level::FindComponentData(object, "MeshRendererComponent");
            if (renderer == nullptr)
                return nullptr;
            const NS::GameCore::Level::FieldValue* material = NS::GameCore::Level::FindField(*renderer, "Material");
            if (material != nullptr && std::holds_alternative<std::string>(material->value))
                return &std::get<std::string>(material->value);
            return nullptr;
        }
    } // namespace

    std::vector<NS::GameCore::Level::ComponentData> MakeGridCubeComponents()
    {
        using namespace NS::GameCore::Level;
        return {MeshRendererData("cube", "", kSolidBaseColor),
                MakeComponentData("BoxColliderComponent", {FieldValue{"Half Extents", kCellHalfExtents}})};
    }

    std::vector<NS::GameCore::Level::ComponentData> MakeGridSlopeComponents(float angleDegrees)
    {
        using namespace NS::GameCore::Level;
        // 角度に対応する楔 builtin メッシュを選び、 見た目の傾斜と当たりの傾斜を一致させる
        const char* meshName = "wedge45";
        if (angleDegrees < 18.75f)
            meshName = "wedge15";
        else if (angleDegrees < 26.25f)
            meshName = "wedge22";
        else if (angleDegrees < 37.5f)
            meshName = "wedge30";
        // grid 固形でなく per-object 描画なので material は free 経路と同じ空参照に倒す
        return {
            MeshRendererData(meshName, "", kSolidBaseColor),
            MakeComponentData("SlopeColliderComponent",
                              {FieldValue{"Angle (deg)", angleDegrees}, FieldValue{"Half Extents", kCellHalfExtents}})};
    }

    std::vector<NS::GameCore::Level::ComponentData> MakeGoalComponents()
    {
        using namespace NS::GameCore::Level;
        // goal pickup は視覚を持たないため、 editor で識別できるよう金色 cube を載せる
        return {MeshRendererData("cube", "", NS::Math::Vector3{1.0f, 0.84f, 0.0f}),
                MakeComponentData("PickupComponent", {FieldValue{"Pickup Kind", 1}})};
    }

    std::vector<NS::GameCore::Level::ComponentData> MakeDefaultPlayerComponents()
    {
        using namespace NS::GameCore::Level;
        // mesh / material 参照は実プレイヤーの直組みと同じ cube + 共有 player 材質。 データ単体でも構成が読める
        return {MeshRendererData("cube", "player", kPlayerBaseColor),
                MakeComponentData("CharacterMovementComponent", {}),
                MakeComponentData("PlayerInputComponent", {}),
                MakeComponentData("ShadowComponent", {})};
    }

    std::vector<NS::GameCore::Level::ComponentData> MakeFollowCameraComponents(std::uint32_t targetObjectId)
    {
        using namespace NS::GameCore::Level;
        // Far Plane 100 はプレイの遠景を抑える投影値。 感触の距離 / 感度はコード既定に任せる
        return {MakeComponentData(
            "ThirdPersonFollowComponent",
            {FieldValue{"Target", NS::Scene::ObjectRef{targetObjectId}}, FieldValue{"Far Plane", 100.0f}})};
    }

    std::vector<NS::GameCore::Level::ComponentData> MakeFreeCubeComponents(
        const NS::GameCore::Level::ObjectInstance& object)
    {
        using namespace NS::GameCore::Level;
        std::vector<ComponentData> result;

        const NS::Math::Vector3 offset{object.colliderOffsetX, object.colliderOffsetY, object.colliderOffsetZ};
        const NS::Math::Vector3 half{
            object.colliderHalfExtentsX, object.colliderHalfExtentsY, object.colliderHalfExtentsZ};
        const NS::Math::Quaternion rotation{
            object.colliderRotationX, object.colliderRotationY, object.colliderRotationZ, object.colliderRotationW};
        const NS::Math::Vector3 rotationEuler = QuaternionToEulerDegrees(rotation);

        result.push_back(MeshRendererData("cube", "", kSolidBaseColor));

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
        else
        {
            // Box / Mesh 形状は回転込み当たり箱で受ける
            result.push_back(MakeComponentData("BoxColliderComponent",
                                               {FieldValue{"Half Extents", half},
                                                FieldValue{"Center Offset", offset},
                                                FieldValue{"Rotation (deg)", rotationEuler}}));
        }
        return result;
    }

    std::optional<NS::Math::AABB> ColliderWorldAABB(NS::Scene::GameObject& obj) noexcept
    {
        if (auto* box = FindComponent<NS::Scene::BoxColliderComponent>(obj))
            return box->WorldAABB();
        if (auto* sphere = FindComponent<NS::Scene::SphereColliderComponent>(obj))
            return sphere->WorldAABB();
        if (auto* capsule = FindComponent<NS::Scene::CapsuleColliderComponent>(obj))
            return capsule->WorldAABB();
        if (auto* slope = FindComponent<NS::Scene::SlopeColliderComponent>(obj))
        {
            const auto tris = slope->WorldTriangles();
            NS::Math::Vector3 lo = tris[0].v0;
            NS::Math::Vector3 hi = tris[0].v0;
            for (const auto& t : tris)
                for (const NS::Math::Vector3& v : {t.v0, t.v1, t.v2})
                {
                    lo = NS::Math::Vector3::Min(lo, v);
                    hi = NS::Math::Vector3::Max(hi, v);
                }
            return NS::Math::AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
        }
        return std::nullopt;
    }

    bool IsGridSolidObject(const NS::GameCore::Level::ObjectInstance& object)
    {
        using namespace NS::GameCore::Level;
        if ((object.flags & kObjectFlagGridAligned) == 0)
            return false;
        // 拾得 / slope / hazard は固形でない。 残る BoxCollider 持ちだけが固形 block
        if (PickupKindOf(object) >= 0)
            return false;
        if (HasComponentType(object, "SlopeColliderComponent"))
            return false;
        if (HasComponentType(object, "HazardComponent"))
            return false;
        return HasComponentType(object, "BoxColliderComponent");
    }

    bool IsRotatableObject(const NS::GameCore::Level::ObjectInstance& object)
    {
        // R で 90° 回す対象。 向きが意味を持つ slope と固形 block。 水 / 装飾は除く
        return SlopeAngleOf(object) >= 0.0f || IsGridSolidObject(object);
    }

    const char* ObjectDisplayName(const NS::GameCore::Level::ObjectInstance& object)
    {
        if (NS::GameCore::Level::IsPlayerObject(object))
            return "Player";
        if (HasComponentType(object, "ThirdPersonFollowComponent"))
            return "Follow Camera";
        if (HasComponentType(object, "PlacedVirtualCamera"))
            return "Camera";

        const int pickupKind = PickupKindOf(object);
        if (pickupKind == 0)
            return "Coin";
        if (pickupKind == 1)
            return "Goal";
        if (pickupKind > 1)
            return "Pickup";

        const float slopeAngle = SlopeAngleOf(object);
        if (slopeAngle >= 0.0f)
        {
            if (slopeAngle >= 44.0f)
                return "Slope 45";
            if (slopeAngle >= 29.0f)
                return "Slope 30";
            if (slopeAngle >= 22.0f)
                return "Slope 22.5";
            if (slopeAngle >= 14.0f)
                return "Slope 15";
            return "Slope";
        }

        if (HasComponentType(object, "HazardComponent"))
            return "Hazard";
        if (const std::string* materialRef = MaterialRefOf(object); materialRef != nullptr && *materialRef == "water")
            return "Water";
        if (HasComponentType(object, "BoxColliderComponent"))
            return "Solid";
        if (HasComponentType(object, "MeshRendererComponent"))
            return "Decoration";
        return "?";
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
        // builtin 名 cube / wedge45 / wedge30 / wedge22 / wedge15 を先引きする
        if (NS::Graphics::StaticMesh* builtin = assets.Builtin(meshRef))
            return builtin;
        // builtin に無ければ ContentRoot 配下の相対パスとして glTF を読む。 .. の traversal は弾かれ nullptr
        const std::optional<std::filesystem::path> resolved = ResolveContentPath(meshRef);
        if (!resolved)
            return nullptr;
        return assets.GetOrLoadMesh(*resolved);
    }

    std::unique_ptr<NS::Scene::GameObject> BuildPlacedObject(const NS::GameCore::Level::ObjectInstance& object,
                                                             NS::Scene::AssetManager& assets,
                                                             const std::vector<std::string>& materialPaths)
    {
        // 全 load 源が component を持って到達する。 JSON / BLKS は移行で、 seed / 配置は materialize 済
        // 空構成は未対応につき配置物として組まない
        if (object.components.empty())
            return nullptr;

        // プレイヤーだけ器を Player 派生にする。 組み方は他の配置物と同一で、 型の分岐はファクトリに閉じる
        const bool isPlayer = NS::GameCore::Level::IsPlayerObject(object);
        std::unique_ptr<NS::Scene::GameObject> obj;
        if (isPlayer)
            obj = std::make_unique<Player>();
        else
            obj = std::make_unique<NS::Scene::GameObject>();

        ApplyComponentsFromData(*obj, object, assets, materialPaths);

        // プレイヤーの移動と入力は休止で組む。 起こすのはプレイ突入の進行役で、 編集中は寝たまま見た目だけ出る
        if (isPlayer)
        {
            if (auto* movement = obj->FindComponent<NS::Scene::CharacterMovementComponent>())
                movement->SetActive(false);
            if (auto* input = obj->FindComponent<NS::Scene::PlayerInputComponent>())
                input->SetActive(false);
        }

        obj->Root().SetPosition(NS::Math::Vector3{object.positionX, object.positionY, object.positionZ});
        obj->Root().SetRotation(
            NS::Math::Quaternion{object.rotationX, object.rotationY, object.rotationZ, object.rotationW});
        obj->Root().SetScale(NS::Math::Vector3{object.scaleX, object.scaleY, object.scaleZ});

        return obj;
    }
} // namespace NS::GameCore::Blocks

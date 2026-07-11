#include "Game/Blocks/BuildPlacedObject.h"

#include "Framework/Scene/ObjectBuilder.h"
#include "Game/Level/LevelObjects.h"
#include "Game/Player.h"

namespace NS::Game::Blocks
{
    namespace
    {
        // 1m grid セルの半径。 cube の grid 当たり箱に使う
        constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

        // free 配置物の material を解決する。 materialIndex 無効 / traversal は共有 player material に倒す
        NS::Graphics::Material* ResolveFreeMaterial(const NS::Scene::ObjectData& object,
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

        // player / water / shadow の共有 material 名なら true、 それ以外は旧 block 含め .mat パス /
        // 既定の自由材質へ倒す
        bool IsSharedMaterialName(const std::string& ref) noexcept
        {
            return ref == "player" || ref == "water" || ref == "shadow";
        }

        // 資産解決の仕上げ。 mesh / material / 影資源はゲームの資産都合なので、 汎用構築から切り離しここに残す
        void ResolveComponentAssets(NS::Scene::Component& created,
                                    const NS::Scene::ObjectData& object,
                                    NS::Scene::AssetManager& assets,
                                    const std::vector<std::string>& materialPaths)
        {
            // material 参照が共有名なら共有 material、 空なら materialIndex / 既定へ倒す。 mesh はメッシュ参照を
            // 参照優先で解決し、 空 / 解決不可なら cube へフォールバックする
            if (auto* mesh = NS::Scene::ComponentCast<NS::Scene::MeshRendererComponent>(&created))
            {
                const std::string& matRef = mesh->MaterialRef();
                NS::Graphics::Material* material = nullptr;
                if (IsSharedMaterialName(matRef))
                    material = assets.SharedMaterial(matRef);
                else
                    material = ResolveFreeMaterial(object, assets, materialPaths);
                mesh->SetMaterial(material);
                // 移行済データは必ず参照を持つ
                NS::Graphics::Mesh* resolved = nullptr;
                if (!mesh->MeshRef().empty())
                    resolved = ResolveMeshFromRef(assets, mesh->MeshRef());
                if (resolved == nullptr)
                    resolved = assets.Builtin("cube");
                mesh->SetMesh(resolved);
            }
            // 接地影の共有資源はファクトリが賄う。 影は常に組み込み quad + 共有 shadow 材質で描く
            else if (auto* shadow = NS::Scene::ComponentCast<NS::Scene::ShadowComponent>(&created))
                shadow->SetResources(assets.Builtin("shadowQuad"), assets.SharedMaterial("shadow"));
        }

        NS::Scene::ComponentData MakeComponentData(std::string typeName, std::vector<NS::Scene::FieldValue> fields)
        {
            NS::Scene::ComponentData component;
            component.typeName = std::move(typeName);
            component.fields = std::move(fields);
            return component;
        }

        NS::Scene::ComponentData MeshRendererData(std::string meshName,
                                                  std::string materialName,
                                                  const NS::Math::Vector3& baseColor)
        {
            return MakeComponentData("MeshRendererComponent",
                                     {NS::Scene::FieldValue{"Mesh", std::move(meshName)},
                                      NS::Scene::FieldValue{"Material", std::move(materialName)},
                                      NS::Scene::FieldValue{"Base Color", baseColor}});
        }

        bool HasComponentType(const NS::Scene::ObjectData& object, const char* typeName) noexcept
        {
            return NS::Scene::FindComponentData(object, typeName) != nullptr;
        }

        // 固形箱の線引き。 BoxCollider を持ち slope / hazard / 拾得を兼ねない箱だけを歩ける固形とする
        // data 側 IsSolidObject と live 側 SolidBoxWorldOBB が同じ規則を 1 箇所で共有する
        bool IsSolidBoxRule(bool hasBox, bool hasSlope, bool hasHazard, bool hasPickup) noexcept
        {
            return hasBox && !hasSlope && !hasHazard && !hasPickup;
        }

        // SlopeColliderComponent の "Angle (deg)" を返す。 SlopeCollider 無しは -1
        float SlopeAngleOf(const NS::Scene::ObjectData& object) noexcept
        {
            const NS::Scene::ComponentData* slope = NS::Scene::FindComponentData(object, "SlopeColliderComponent");
            if (slope == nullptr)
                return -1.0f;
            const NS::Scene::FieldValue* angle = NS::Scene::FindField(*slope, "Angle (deg)");
            if (angle != nullptr && std::holds_alternative<float>(angle->value))
                return std::get<float>(angle->value);
            return 0.0f;
        }

        // MeshRenderer の "Material" 参照を返す。 MeshRenderer / フィールド無しは nullptr
        const std::string* MaterialRefOf(const NS::Scene::ObjectData& object) noexcept
        {
            const NS::Scene::ComponentData* renderer = NS::Scene::FindComponentData(object, "MeshRendererComponent");
            if (renderer == nullptr)
                return nullptr;
            const NS::Scene::FieldValue* material = NS::Scene::FindField(*renderer, "Material");
            if (material != nullptr && std::holds_alternative<std::string>(material->value))
                return &std::get<std::string>(material->value);
            return nullptr;
        }
    } // namespace

    std::vector<NS::Scene::ComponentData> MakeCellCubeComponents()
    {
        return {MeshRendererData("cube", "", kSolidBaseColor),
                MakeComponentData("BoxColliderComponent", {NS::Scene::FieldValue{"Half Extents", kCellHalfExtents}})};
    }

    std::vector<NS::Scene::ComponentData> MakeCellSlopeComponents(float angleDegrees)
    {
        // 角度に対応する楔 builtin メッシュを選び、 見た目の傾斜と当たりの傾斜を一致させる
        const char* meshName = "wedge45";
        if (angleDegrees < 18.75f)
            meshName = "wedge15";
        else if (angleDegrees < 26.25f)
            meshName = "wedge22";
        else if (angleDegrees < 37.5f)
            meshName = "wedge30";
        // grid 固形でなく per-object 描画なので material は free 経路と同じ空参照に倒す
        return {MeshRendererData(meshName, "", kSolidBaseColor),
                MakeComponentData("SlopeColliderComponent",
                                  {NS::Scene::FieldValue{"Angle (deg)", angleDegrees},
                                   NS::Scene::FieldValue{"Half Extents", kCellHalfExtents}})};
    }

    std::vector<NS::Scene::ComponentData> MakeGoalComponents()
    {
        // goal pickup は視覚を持たないため、 editor で識別できるよう金色 cube を載せる
        return {MeshRendererData("cube", "", NS::Math::Vector3{1.0f, 0.84f, 0.0f}),
                MakeComponentData("PickupComponent", {NS::Scene::FieldValue{"Pickup Kind", 1}})};
    }

    std::vector<NS::Scene::ComponentData> MakeDefaultPlayerComponents()
    {
        // mesh / material 参照は実プレイヤーの直組みと同じ cube + 共有 player 材質。 データ単体でも構成が読める
        return {MeshRendererData("cube", "player", kPlayerBaseColor),
                MakeComponentData("CharacterMovementComponent", {}),
                MakeComponentData("PlayerInputComponent", {}),
                MakeComponentData("ShadowComponent", {})};
    }

    std::vector<NS::Scene::ComponentData> MakeFollowCameraComponents(std::uint32_t targetObjectId)
    {
        // Far Plane 100 はプレイの遠景を抑える投影値。 感触の距離 / 感度はコード既定に任せる
        return {MakeComponentData("ThirdPersonFollowComponent",
                                  {NS::Scene::FieldValue{"Target", NS::Scene::ObjectRef{targetObjectId}},
                                   NS::Scene::FieldValue{"Far Plane", 100.0f}})};
    }

    std::vector<NS::Scene::ComponentData> MakeFreeCubeComponents()
    {
        // 自由配置の既定は cell の素 cube と同じ構成。 寸法・形状は配置後の component 編集で変える
        return MakeCellCubeComponents();
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

    std::optional<NS::Physics::OBB> SolidBoxWorldOBB(NS::Scene::GameObject& obj) noexcept
    {
        auto* box = FindComponent<NS::Scene::BoxColliderComponent>(obj);
        const bool hasSlope = FindComponent<NS::Scene::SlopeColliderComponent>(obj) != nullptr;
        const bool hasHazard = FindComponent<NS::Scene::HazardComponent>(obj) != nullptr;
        const bool hasPickup = FindComponent<NS::Scene::PickupComponent>(obj) != nullptr;
        if (!IsSolidBoxRule(box != nullptr, hasSlope, hasHazard, hasPickup))
            return std::nullopt;
        return box->WorldOBB();
    }

    bool IsSolidObject(const NS::Scene::ObjectData& object)
    {
        const bool hasBox = HasComponentType(object, "BoxColliderComponent");
        const bool hasSlope = HasComponentType(object, "SlopeColliderComponent");
        const bool hasHazard = HasComponentType(object, "HazardComponent");
        const bool hasPickup = NS::Game::Level::PickupKindOf(object) >= 0;
        return IsSolidBoxRule(hasBox, hasSlope, hasHazard, hasPickup);
    }

    bool IsRotatableObject(const NS::Scene::ObjectData& object)
    {
        // R で 90° 回す対象。 向きが意味を持つ slope と固形 block。 水 / 装飾は除く
        return SlopeAngleOf(object) >= 0.0f || IsSolidObject(object);
    }

    const char* ObjectDisplayName(const NS::Scene::ObjectData& object)
    {
        if (NS::Game::Level::IsPlayerObject(object))
            return "Player";
        if (HasComponentType(object, "ThirdPersonFollowComponent"))
            return "Follow Camera";
        if (HasComponentType(object, "PlacedVirtualCamera"))
            return "Camera";

        const int pickupKind = NS::Game::Level::PickupKindOf(object);
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

    std::unique_ptr<NS::Scene::GameObject> BuildPlacedObject(const NS::Scene::ObjectData& object,
                                                             NS::Scene::AssetManager& assets,
                                                             const std::vector<std::string>& materialPaths)
    {
        // 全 load 源が component を持って到達する。 JSON / BLKS は移行で、 seed / 配置は materialize 済
        // 空構成は未対応につき配置物として組まない
        if (object.components.empty())
            return nullptr;

        // プレイヤーだけ器を Player 派生にする。 組み方は他の配置物と同一で、 型の分岐はファクトリに閉じる
        const bool isPlayer = NS::Game::Level::IsPlayerObject(object);
        std::unique_ptr<NS::Scene::GameObject> obj;
        if (isPlayer)
            obj = std::make_unique<Player>();
        else
            obj = std::make_unique<NS::Scene::GameObject>();

        NS::Scene::ApplyObjectComponents(
            *obj, object, [&](NS::Scene::Component& created, const NS::Scene::ComponentData&) {
                ResolveComponentAssets(created, object, assets, materialPaths);
            });

        // プレイヤーの移動と入力は休止で組む。 起こすのはプレイ突入の進行役で、 編集中は寝たまま見た目だけ出る
        if (isPlayer)
        {
            if (auto* movement = obj->FindComponent<NS::Scene::CharacterMovementComponent>())
                movement->SetActive(false);
            if (auto* input = obj->FindComponent<NS::Scene::PlayerInputComponent>())
                input->SetActive(false);
        }

        NS::Scene::ApplyObjectTransform(*obj, object);

        return obj;
    }
} // namespace NS::Game::Blocks

#include "Editor/PlacementCatalog.h"

#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <memory>
#include <optional>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // クラスの OnInit が積む構成をそのまま写したひな形。値はコード既定に任せる
            [[nodiscard]] nlohmann::json PrototypeOf(const SceneActor& actor, std::string_view className)
            {
                nlohmann::json prototype = MakePrototypeJson(actor);
                SetObjectJsonClass(prototype, className);
                return prototype;
            }

            [[nodiscard]] nlohmann::json PrototypeOf(const TypeRegistry::Entry& entry)
            {
                const std::unique_ptr<SceneActor> actor = CreateActorOfClass(entry.className);
                return PrototypeOf(*actor, entry.className);
            }

            // 地形の部品のひな形は、コード既定に任せる欄のほかにメッシュの欄だけを持つ。
            // メッシュはアクターの Model に書き、部品の保存の鍵と欄の名前はアクターの部品と欄の登録から引く
            [[nodiscard]] nlohmann::json PartsPrototypeOf(const TypeRegistry::Entry& entry, std::string_view meshName)
            {
                const std::unique_ptr<SceneActor> actor = CreateActorOfClass(entry.className);
                nlohmann::json prototype = PrototypeOf(*actor, entry.className);
                Model* model = actor->ModelClassObj();
                if (model == nullptr)
                {
                    return prototype;
                }
                model->SetMeshRef(meshName);
                const nlohmann::json fields = SerializeClassObjFields(*model);
                const ReflectionInfo* info = model->GetReflection();
                ForEachReflectedClassObj(*actor, [&](const ClassObjInfo& row, Object* classObj) {
                    if (classObj != model)
                    {
                        return;
                    }
                    nlohmann::json& part = ObjectJsonClassObjs(prototype)[row.key];
                    for (std::size_t i = 0; i < info->fieldCount; ++i)
                    {
                        const FieldDesc& field = info->fields[i];
                        if (field.assetKind == AssetKind::Mesh)
                        {
                            part[field.name] = fields[field.name];
                        }
                    }
                });
                return prototype;
            }

            // 地形の部品のメッシュ違いを並べる。当たりはメッシュに付いて来る
            [[nodiscard]] PlacementItem MakePartsItem(std::string_view label,
                                                      const TypeRegistry::Entry& partsEntry,
                                                      std::string_view meshName,
                                                      bool rotatable)
            {
                return PlacementItem{std::string(label), PartsPrototypeOf(partsEntry, meshName), rotatable};
            }

            [[nodiscard]] std::vector<PlacementItem> BuildItems()
            {
                std::vector<PlacementItem> items;
                for (const TypeRegistry::Entry* entry : PlaceableEntries())
                {
                    if (std::string_view{entry->className} != "MapPartsActor")
                    {
                        items.push_back(PlacementItem{entry->label, PrototypeOf(*entry), false});
                        continue;
                    }

                    // 地形の部品は形ごとに並べる。組み込みの形はどれも 1m のセルに合わせた大きさ
                    items.push_back(MakePartsItem(k_PartsCubeLabel, *entry, "cube", true));
                    items.push_back(MakePartsItem("地形の部品 (球)", *entry, "sphere", false));
                    items.push_back(MakePartsItem(k_PartsSlopeLabel, *entry, "wedge45", true));
                }
                return items;
            }
        } // namespace

        const std::vector<PlacementItem>& PlacementItems()
        {
            // 登録は main 前の静的初期化で済むので、初回に確定した一覧を使い回せる
            static const std::vector<PlacementItem> items = BuildItems();
            return items;
        }

        const PlacementItem* FindPlacementItem(std::string_view label) noexcept
        {
            const std::vector<PlacementItem>& items = PlacementItems();
            const std::vector<PlacementItem>::const_iterator it =
                std::ranges::find(items, label, &PlacementItem::label);
            if (it == items.end())
            {
                return nullptr;
            }
            return &*it;
        }

        nlohmann::json MakeMeshPartsPrototype(std::string_view meshRef)
        {
            const TypeRegistry::Entry* entry = TypeRegistry::Get().Find("MapPartsActor");
            if (entry == nullptr || entry->create == nullptr)
            {
                return MakeObjectJson();
            }
            return PartsPrototypeOf(*entry, meshRef);
        }

        float PartsSlopeAngleDegrees(const nlohmann::json& prototype) noexcept
        {
            const std::string_view className = ObjectJsonClass(prototype);
            if (className.empty())
            {
                return -1.0f;
            }
            const std::unique_ptr<SceneActor> actor = CreateActorOfClass(className);
            ApplyObjectClassObjs(*actor, prototype);
            const Model* model = actor->ModelClassObj();
            if (model == nullptr)
            {
                return -1.0f;
            }
            const std::optional<float> angle = AssetManager::BuiltinSlopeAngleDegrees(model->MeshRef());
            if (!angle.has_value())
            {
                return -1.0f;
            }
            return *angle;
        }
    } // namespace Editor
} // namespace NS

#include "Editor/PlacementCatalog.h"

#include "Editor/EditorObjects.h"

#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // 地形の部品のひな形は、.actor に任せる欄のほかにメッシュの欄だけを持つ。
            // メッシュはアクターの Model に書き、部品の保存の鍵と欄の名前はアクターの部品と欄の登録から引く
            [[nodiscard]] nlohmann::json PartsPrototypeOf(SceneActor& actor, std::string_view meshName)
            {
                nlohmann::json prototype = MakePrototypeJson(actor);
                Model* model = actor.ModelClassObj();
                if (model == nullptr)
                {
                    return prototype;
                }
                model->SetMeshRef(meshName);
                const nlohmann::json fields = SerializeClassObjFields(*model);
                const ReflectionInfo* info = model->GetReflection();
                ForEachReflectedClassObj(actor, [&](const ClassObjInfo& row, Object* classObj) {
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
                                                      std::string_view actorFileName,
                                                      std::string_view className,
                                                      std::string_view meshName,
                                                      bool rotatable)
            {
                const std::unique_ptr<SceneActor> actor =
                    CreateActorOfClass(className, ActorInitInfo{.actorFile = actorFileName});
                return PlacementItem{std::string(label), PartsPrototypeOf(*actor, meshName), rotatable};
            }
        } // namespace

        std::vector<PlacementItem> PlacementItems()
        {
            // .actor は試しが敷いた物や置き場の差し替えで変わるので、控えを持たずに毎回組む
            ActorFileLibrary& library = ActorFileLibrary::Get();
            std::vector<PlacementItem> items;
            for (const std::string& name : library.Names())
            {
                // 置けるのは NS_PLACEABLE で登録したクラスの .actor だけ。自機の .actor は並べない
                const std::string className = library.ClassNameOf(name);
                const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(className);
                if (entry == nullptr || entry->label == nullptr)
                {
                    continue;
                }
                // 型から作る地形の部品と同じ .actor だけを、メッシュの形ごとに並べる
                if (name != "MapPartsActor")
                {
                    items.push_back(PlacementItem{name, MakePrototypeJson(library.Baseline(name)), false});
                    continue;
                }

                // 組み込みの形はどれも 1m のセルに合わせた大きさ
                items.push_back(MakePartsItem(k_PartsCubeLabel, name, className, "cube", true));
                items.push_back(MakePartsItem("地形の部品 (球)", name, className, "sphere", false));
                items.push_back(MakePartsItem(k_PartsSlopeLabel, name, className, "wedge45", true));
            }
            return items;
        }

        std::optional<PlacementItem> FindPlacementItem(std::string_view label)
        {
            std::vector<PlacementItem> items = PlacementItems();
            const std::vector<PlacementItem>::iterator it = std::ranges::find(items, label, &PlacementItem::label);
            if (it == items.end())
            {
                return std::nullopt;
            }
            return std::move(*it);
        }

        nlohmann::json MakeMeshPartsPrototype(std::string_view meshRef)
        {
            const TypeRegistry::Entry* entry = TypeRegistry::Get().Find("MapPartsActor");
            if (entry == nullptr || entry->create == nullptr)
            {
                return MakeObjectJson();
            }
            // 型から作るのでクラスと同じ名前の .actor を敷く
            const std::unique_ptr<SceneActor> actor = CreateActorOfClass(entry->className);
            return PartsPrototypeOf(*actor, meshRef);
        }

        float PartsSlopeAngleDegrees(const nlohmann::json& prototype) noexcept
        {
            const std::string_view actorFileName = ObjectJsonActorFile(prototype);
            const std::string className = ActorFileLibrary::Get().ClassNameOf(actorFileName);
            if (className.empty())
            {
                return -1.0f;
            }
            const std::unique_ptr<SceneActor> actor =
                CreateActorOfClass(className, ActorInitInfo{.actorFile = actorFileName});
            if (actor == nullptr)
            {
                return -1.0f;
            }
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

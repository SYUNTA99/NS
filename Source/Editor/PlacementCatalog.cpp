#include "Editor/PlacementCatalog.h"

#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // クラスの OnInit が積む構成をそのまま写したひな形。クラスと同じ名前の .actor を指す
            [[nodiscard]] nlohmann::json PrototypeOf(const TypeRegistry::Entry& entry)
            {
                const std::unique_ptr<SceneActor> actor = CreateActorOfClass(entry.className);
                return MakePrototypeJson(*actor);
            }

            void SetPartsMesh(nlohmann::json& prototype, std::string_view meshName)
            {
                if (nlohmann::json* renderer = ClassObjFields(prototype, "Model"))
                {
                    SetField(*renderer, "メッシュ", meshName);
                }
            }

            // 地形の部品のメッシュ違いを並べる。当たりはメッシュに付いて来る
            [[nodiscard]] PlacementItem MakePartsItem(std::string_view label,
                                                      const nlohmann::json& partsPrototype,
                                                      std::string_view meshName,
                                                      bool rotatable)
            {
                PlacementItem item{std::string(label), partsPrototype, rotatable};
                SetPartsMesh(item.prototype, meshName);
                return item;
            }

            // 一覧を組む元。.actor の名前とその中の class
            using ActorFileClasses = std::vector<std::pair<std::string, std::string>>;

            [[nodiscard]] ActorFileClasses ReadActorFileClasses()
            {
                ActorFileLibrary& library = ActorFileLibrary::Get();
                ActorFileClasses classes;
                for (std::string& name : library.Names())
                {
                    std::string className = library.ClassNameOf(name);
                    classes.emplace_back(std::move(name), std::move(className));
                }
                return classes;
            }

            [[nodiscard]] std::vector<PlacementItem> BuildItems(const ActorFileClasses& classes)
            {
                std::vector<PlacementItem> items;
                for (const std::pair<std::string, std::string>& actorFile : classes)
                {
                    // 置けるのは NS_PLACEABLE で登録したクラスの .actor だけ。自機の .actor は並べない
                    const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(actorFile.second);
                    if (entry == nullptr || entry->label == nullptr)
                    {
                        continue;
                    }
                    const nlohmann::json prototype = MakePrototypeJson(ActorFileLibrary::Get().Baseline(actorFile.first));
                    // 型から作る地形の部品と同じ .actor だけを、メッシュの形ごとに並べる
                    if (actorFile.first != "MapPartsActor")
                    {
                        items.push_back(PlacementItem{actorFile.first, prototype, false});
                        continue;
                    }

                    // 組み込みの形はどれも 1m のセルに合わせた大きさ
                    items.push_back(MakePartsItem(k_PartsCubeLabel, prototype, "cube", true));
                    items.push_back(MakePartsItem("地形の部品 (球)", prototype, "sphere", false));
                    items.push_back(MakePartsItem(k_PartsSlopeLabel, prototype, "wedge45", true));
                }
                return items;
            }
        } // namespace

        const std::vector<PlacementItem>& PlacementItems()
        {
            // .actor は試しが敷いた物や置き場の差し替えで変わるので、名前とクラスが変わった時に組み直す
            static ActorFileClasses builtFrom;
            static std::vector<PlacementItem> items;
            ActorFileClasses current = ReadActorFileClasses();
            if (current != builtFrom)
            {
                items = BuildItems(current);
                builtFrom = std::move(current);
            }
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
            nlohmann::json prototype = MakeObjectJson();
            const TypeRegistry::Entry* entry = TypeRegistry::Get().Find("MapPartsActor");
            if (entry != nullptr && entry->create != nullptr)
            {
                prototype = PrototypeOf(*entry);
            }
            SetPartsMesh(prototype, meshRef);
            return prototype;
        }

        float PartsSlopeAngleDegrees(const nlohmann::json& prototype) noexcept
        {
            const nlohmann::json* renderer = ClassObjFields(prototype, "Model");
            if (renderer == nullptr)
            {
                return -1.0f;
            }
            // 組み込みの坂のメッシュの名前と角度の対応。AssetManager の組み込みの形と揃える
            const std::string mesh = FieldString(*renderer, "メッシュ", "");
            if (mesh == "wedge45")
            {
                return 45.0f;
            }
            if (mesh == "wedge30")
            {
                return 30.0f;
            }
            if (mesh == "wedge22")
            {
                return 22.5f;
            }
            if (mesh == "wedge15")
            {
                return 15.0f;
            }
            return -1.0f;
        }
    } // namespace Editor
} // namespace NS

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
        } // namespace

        std::vector<PlacementItem> PlacementItems()
        {
            // .actor は試しが敷いた物や置き場の差し替えで変わるので、控えを持たずに毎回組む
            ActorFileLibrary& library = ActorFileLibrary::Get();
            std::vector<PlacementItem> items;
            for (const std::string& name : library.Names())
            {
                // 置けるのは NS_PLACEABLE で登録したクラスの .actor だけ。自機の .actor は並べない
                const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(library.ClassNameOf(name));
                if (entry == nullptr || entry->label == nullptr)
                {
                    continue;
                }
                const nlohmann::json prototype = MakePrototypeJson(library.Baseline(name));
                // 型から作る地形の部品と同じ .actor だけを、メッシュの形ごとに並べる
                if (name != "MapPartsActor")
                {
                    items.push_back(PlacementItem{name, prototype, false});
                    continue;
                }

                // 組み込みの形はどれも 1m のセルに合わせた大きさ
                items.push_back(MakePartsItem(k_PartsCubeLabel, prototype, "cube", true));
                items.push_back(MakePartsItem("地形の部品 (球)", prototype, "sphere", false));
                items.push_back(MakePartsItem(k_PartsSlopeLabel, prototype, "wedge45", true));
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

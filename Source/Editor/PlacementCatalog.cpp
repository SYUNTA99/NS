#include "Editor/PlacementCatalog.h"

#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/SubObjectEntry.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <memory>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            // クラスの OnInit が積む構成をそのまま写したひな形。値はコード既定に任せる
            [[nodiscard]] nlohmann::json PrototypeOf(const TypeRegistry::Entry& entry)
            {
                const std::unique_ptr<SceneActor> actor = CreateActorOfClass(entry.className);
                nlohmann::json prototype = MakePrototypeJson(*actor);
                SetObjectJsonClass(prototype, entry.className);
                return prototype;
            }

            void SetPartsMesh(nlohmann::json& prototype, std::string_view meshName)
            {
                if (nlohmann::json* renderer = SubObjFields(prototype, "Model"))
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
                    const nlohmann::json parts = PrototypeOf(*entry);
                    items.push_back(MakePartsItem(k_PartsCubeLabel, parts, "cube", true));
                    items.push_back(MakePartsItem("地形の部品 (球)", parts, "sphere", false));
                    items.push_back(MakePartsItem(k_PartsSlopeLabel, parts, "wedge45", true));
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
            const nlohmann::json* renderer = SubObjFields(prototype, "Model");
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

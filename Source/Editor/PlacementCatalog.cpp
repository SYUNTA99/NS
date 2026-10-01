#include "Editor/PlacementCatalog.h"

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <memory>

namespace NS::Editor
{
    namespace
    {
        // クラスのコンストラクタが積む構成をそのまま写したひな形。値はコード既定に任せる
        [[nodiscard]] nlohmann::json PrototypeOf(const NS::Obj::TypeRegistry::Entry& entry)
        {
            const std::unique_ptr<NS::Obj::Actor> actor = entry.create();
            nlohmann::json prototype = NS::Obj::MakePrototypeJson(*actor);
            NS::Obj::SetObjectJsonClass(prototype, entry.className);
            return prototype;
        }

        // 地形の部品の見た目のメッシュを差し替える
        void SetPartsMesh(nlohmann::json& prototype, std::string_view meshName)
        {
            if (nlohmann::json* renderer = NS::Obj::PartFields(prototype, "Model"))
            {
                NS::Obj::SetField(*renderer, "メッシュ", meshName);
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
            for (const NS::Obj::TypeRegistry::Entry* entry : NS::Obj::PlaceableEntries())
            {
                if (std::string_view{entry->className} != "MapParts")
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
        for (const PlacementItem& item : PlacementItems())
        {
            if (item.label == label)
            {
                return &item;
            }
        }
        return nullptr;
    }

    nlohmann::json MakeMeshPartsPrototype(std::string_view meshRef)
    {
        nlohmann::json prototype = NS::Obj::MakeObjectJson();
        const NS::Obj::TypeRegistry::Entry* entry = NS::Obj::TypeRegistry::Get().Find("MapParts");
        if (entry != nullptr && entry->create != nullptr)
        {
            prototype = PrototypeOf(*entry);
        }
        SetPartsMesh(prototype, meshRef);
        return prototype;
    }

    float PartsSlopeAngleDegrees(const nlohmann::json& prototype) noexcept
    {
        const nlohmann::json* renderer = NS::Obj::PartFields(prototype, "Model");
        if (renderer == nullptr)
        {
            return -1.0f;
        }
        // 組み込みの坂のメッシュの名前と角度の対応。AssetManager の組み込みの形と揃える
        const std::string mesh = NS::Obj::FieldString(*renderer, "メッシュ", "");
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
} // namespace NS::Editor

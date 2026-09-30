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
            if (nlohmann::json* renderer = NS::Obj::FindComponentEntry(prototype, "MeshRenderer"))
            {
                NS::Obj::SetField(*renderer, "メッシュ", meshName);
            }
        }

        // 地形の部品に当たりの形を 1 つ足した物を並べる
        [[nodiscard]] PlacementItem MakePartsItem(std::string_view label,
                                                  const nlohmann::json& partsPrototype,
                                                  std::string_view meshName,
                                                  nlohmann::json collider,
                                                  bool rotatable)
        {
            PlacementItem item{std::string(label), partsPrototype, rotatable};
            SetPartsMesh(item.prototype, meshName);
            NS::Obj::ObjectJsonComponents(item.prototype).push_back(std::move(collider));
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

                // 地形の部品は当たりの形ごとに並べる。1m のセルに合わせた大きさ
                const nlohmann::json parts = PrototypeOf(*entry);

                nlohmann::json box = NS::Obj::MakeComponentEntry("BoxCollider");
                NS::Obj::SetField(box, "半径", NS::Core::Vector3{0.5f, 0.5f, 0.5f});
                items.push_back(MakePartsItem(k_PartsCubeLabel, parts, "cube", std::move(box), true));

                nlohmann::json sphere = NS::Obj::MakeComponentEntry("SphereCollider");
                NS::Obj::SetField(sphere, "半径", 0.5f);
                items.push_back(MakePartsItem("地形の部品 (球)", parts, "sphere", std::move(sphere), false));

                nlohmann::json slope = NS::Obj::MakeComponentEntry("SlopeCollider");
                NS::Obj::SetField(slope, "角度 (度)", 45.0f);
                NS::Obj::SetField(slope, "半径", NS::Core::Vector3{0.5f, 0.5f, 0.5f});
                items.push_back(MakePartsItem(k_PartsSlopeLabel, parts, "wedge45", std::move(slope), true));
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
        NS::Obj::ObjectJsonComponents(prototype).push_back(NS::Obj::MakeComponentEntry("MeshCollider"));
        return prototype;
    }
} // namespace NS::Editor

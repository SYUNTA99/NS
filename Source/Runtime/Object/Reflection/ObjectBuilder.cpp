#include "Runtime/Object/Reflection/ObjectBuilder.h"

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>
#include <string>
#include <vector>

namespace NS::Obj
{
    namespace
    {
        // GameObject に既に載る同型 component をリフレクション型名で探す。適用済みの控えにある分は飛ばし、無ければ
        // nullptr
        Component* FindExistingComponent(const GameObject& obj,
                                         std::string_view typeName,
                                         const std::vector<Component*>& applied) noexcept
        {
            for (Component* comp : obj.Components())
            {
                if (comp == nullptr)
                {
                    continue;
                }
                if (std::find(applied.begin(), applied.end(), comp) != applied.end())
                {
                    continue;
                }
                const ReflectionInfo* info = comp->GetReflection();
                if (info != nullptr && typeName == info->typeName)
                {
                    return comp;
                }
            }
            return nullptr;
        }

    } // namespace

    Component* MatchComponentEntry(const GameObject& obj,
                                   const nlohmann::json& entry,
                                   const std::vector<Component*>& taken) noexcept
    {
        const std::string_view typeName = ComponentEntryType(entry);
        if (typeName.empty())
        {
            return nullptr;
        }
        const std::string_view name = ComponentEntryName(entry);
        if (!name.empty())
        {
            Component* named = obj.FindComponentByName(name);
            if (named != nullptr && std::find(taken.begin(), taken.end(), named) == taken.end())
            {
                const ReflectionInfo* info = named->GetReflection();
                if (info != nullptr && typeName == info->typeName)
                {
                    return named;
                }
            }
        }
        return FindExistingComponent(obj, typeName, taken);
    }

    // データを唯一の正とする主経路。既定構成を積む GameObject では値だけが写り二重生成しない
    // データと live は 1 対 1 で対応させる
    void ApplyObjectComponents(GameObject& obj, const ObjectData& object, const ComponentBuiltFn& onBuilt)
    {
        if (!object.components.is_array())
        {
            return;
        }

        std::vector<Component*> applied;
        for (const nlohmann::json& entry : object.components)
        {
            const std::string_view typeName = ComponentEntryType(entry);
            if (typeName.empty())
            {
                continue;
            }

            Component* created = MatchComponentEntry(obj, entry, applied);
            if (created == nullptr)
            {
                created = CreateComponent(std::string(typeName), obj);
            }
            if (created == nullptr)
            {
                continue; // 許可リスト外 / 未知の型は読み飛ばす
            }
            applied.push_back(created);

            // 保存された名前へ付け直す。名前の無い古いデータは作った時の型名のまま
            const std::string_view name = ComponentEntryName(entry);
            if (!name.empty() && created->Name() != name)
            {
                obj.RenameComponent(*created, name);
            }
            created->SetEnabled(ComponentEntryEnabled(entry));

            const nlohmann::json::const_iterator fieldsIt = entry.find("fields");
            if (fieldsIt != entry.end())
            {
                ApplyJsonFields(*created, *fieldsIt);
            }

            if (onBuilt)
            {
                onBuilt(*created, entry);
            }
        }
    }

    std::unique_ptr<GameObject> BuildSceneObject(const ObjectData& object, AssetManager* assets)
    {
        if (object.components.empty())
        {
            return nullptr;
        }

        std::unique_ptr<GameObject> obj = CreateRegisteredObject(object);
        ApplyObjectComponents(*obj, object, {});

        // 参照文字列の実体化は component 自身の仕事。AssetManager が無い間は文字列のまま持たせておく
        if (assets != nullptr)
        {
            for (Component* comp : obj->Components())
            {
                if (comp != nullptr)
                {
                    comp->ResolveAssets(*assets);
                }
            }
        }
        return obj;
    }

    ObjectData MakeObjectData(const GameObject& obj)
    {
        ObjectData data{};
        data.className = obj.ClassName();
        data.name = obj.Name();
        data.active = obj.IsActiveSelf();
        if (const GameObject* parent = obj.Parent())
            data.parentId = parent->Id();
        for (const Component* comp : obj.Components())
        {
            if (comp == nullptr)
            {
                continue;
            }
            const ReflectionInfo* info = comp->GetReflection();
            if (info == nullptr)
            {
                continue;
            }
            nlohmann::json entry = MakeComponentEntry(info->typeName);
            // コンストラクタが付けた名前も写す。同じ型を 2 つ積むクラスで、どちらの件かが名前で決まる
            SetComponentEntryName(entry, comp->Name());
            data.components.push_back(std::move(entry));
        }
        SetObjectPosition(data, obj.Root().Position());
        SetObjectRotation(data, obj.Root().Rotation());
        SetObjectScale(data, obj.Root().Scale());
        return data;
    }

    ObjectData CaptureObjectData(const GameObject& obj)
    {
        ObjectData data{};
        data.className = obj.ClassName();
        data.name = obj.Name();
        data.active = obj.IsActiveSelf();
        if (const GameObject* parent = obj.Parent())
        {
            data.parentId = parent->Id();
        }

        for (const Component* comp : obj.Components())
        {
            if (comp == nullptr)
            {
                continue;
            }
            if (comp->GetReflection() == nullptr)
            {
                continue;
            }

            nlohmann::json entry = SerializeComponent(*comp);
            // id と名前は往復で保つ。id を落とすと保存のたびに振り直しになり、参照が外れる
            SetComponentEntryId(entry, comp->Id());
            SetComponentEntryName(entry, comp->Name());
            // active はデータ側だけを写す。モード切替の一時的な休止 (SetActive) は保存に持ち込まない
            SetComponentEntryEnabled(entry, comp->IsEnabled());
            data.components.push_back(std::move(entry));
        }
        return data;
    }
} // namespace NS::Obj

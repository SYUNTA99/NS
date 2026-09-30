#include "Runtime/Object/Reflection/ObjectBuilder.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/Archetype.h"
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
        // Actor に既に載る同型 component をリフレクション型名で探す。適用済みの控えにある分は飛ばし、無ければ
        // nullptr
        Component* FindExistingComponent(const Actor& obj,
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

    Component* MatchComponentEntry(const Actor& obj,
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

    void ApplyObjectComponents(Actor& obj, const nlohmann::json& object, PartCreation creation)
    {
        std::vector<Component*> applied;
        for (const nlohmann::json& entry : ObjectJsonComponents(object))
        {
            const std::string_view typeName = ComponentEntryType(entry);
            if (typeName.empty())
            {
                continue;
            }

            Component* target = MatchComponentEntry(obj, entry, applied);
            if (target == nullptr)
            {
                if (creation == PartCreation::Forbid)
                {
                    // どの部品を持つかはクラスと種類の既定値が決める。個体のデータは部品を足せない
                    NS_LOG_WARN(Scene, "{} の個体のデータにある部品 {} はクラスに無いので読み飛ばす", obj.ClassName(), typeName);
                    continue;
                }
                target = CreateComponent(std::string(typeName), obj);
            }
            if (target == nullptr)
            {
                continue; // 許可リスト外 / 未知の型は読み飛ばす
            }
            applied.push_back(target);

            // 保存された名前へ付け直す。名前の無い件は作った時の型名のまま
            const std::string_view name = ComponentEntryName(entry);
            if (!name.empty() && target->Name() != name)
            {
                obj.RenameComponent(*target, name);
            }
            target->SetEnabled(ComponentEntryEnabled(entry));

            const nlohmann::json::const_iterator fieldsIt = entry.find("fields");
            if (fieldsIt != entry.end())
            {
                ApplyJsonFields(*target, *fieldsIt);
            }
        }
    }

    std::unique_ptr<Actor> ObjectFromJson(const nlohmann::json& object, AssetManager* assets)
    {
        // 部品はクラスのコンストラクタと種類の既定値が積む。クラスの無い JSON は配置物でない
        if (ObjectJsonClass(object).empty())
        {
            return nullptr;
        }

        // コードの既定値 < 種類の既定値 < 個体の上書き の順に重ねる
        std::unique_ptr<Actor> obj = CreateRegisteredObject(object);
        ApplyArchetype(*obj);
        ApplyObjectComponents(*obj, object, PartCreation::Forbid);

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

    nlohmann::json MakePrototypeJson(const Actor& obj)
    {
        nlohmann::json object = MakeObjectJson();
        SetObjectJsonClass(object, obj.ClassName());
        SetObjectJsonName(object, obj.Name());
        SetObjectJsonActive(object, obj.IsActiveSelf());
        if (const Actor* parent = obj.Parent())
        {
            SetObjectJsonParent(object, parent->Id());
        }
        nlohmann::json& components = ObjectJsonComponents(object);
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
            components.push_back(std::move(entry));
        }
        SetObjectPosition(object, obj.Root().Position());
        SetObjectRotation(object, obj.Root().Rotation());
        SetObjectScale(object, obj.Root().Scale());
        return object;
    }

    nlohmann::json ObjectToJson(const Actor& obj)
    {
        nlohmann::json object = MakeObjectJson();
        SetObjectJsonId(object, obj.Id());
        SetObjectJsonClass(object, obj.ClassName());
        SetObjectJsonName(object, obj.Name());
        SetObjectJsonActive(object, obj.IsActiveSelf());
        if (const Actor* parent = obj.Parent())
        {
            SetObjectJsonParent(object, parent->Id());
        }

        nlohmann::json& components = ObjectJsonComponents(object);
        for (const Component* comp : obj.Components())
        {
            if (comp == nullptr || comp->GetReflection() == nullptr)
            {
                continue;
            }

            nlohmann::json entry = SerializeComponent(*comp);
            // id と名前は往復で保つ。id を落とすと保存のたびに振り直しになり、参照が外れる
            SetComponentEntryId(entry, comp->Id());
            SetComponentEntryName(entry, comp->Name());
            // active はデータ側だけを写す。モード切替の一時的な休止 (SetActive) は保存に持ち込まない
            SetComponentEntryEnabled(entry, comp->IsEnabled());
            components.push_back(std::move(entry));
        }
        // 種類の既定値と同じ欄は書かない。種類の既定値を変えると、上書きしていない個体はみな新しい値になる
        return DiffObjectJson(object);
    }
} // namespace NS::Obj

#include "Runtime/Object/Reflection/ObjectBuilder.h"

#include "Runtime/Math/Math.h"
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

namespace NS::Object
{
    namespace
    {
        // GameObject に既に載る同型 component を反射型名で探す。 適用済みの控えにある分は飛ばし、 無ければ nullptr
        Component* FindExistingComponent(GameObject& obj,
                                         std::string_view typeName,
                                         const std::vector<Component*>& applied)
        {
            for (Component* comp : obj.Components())
            {
                if (comp == nullptr)
                    continue;
                if (std::find(applied.begin(), applied.end(), comp) != applied.end())
                    continue;
                const ReflectionInfo* info = comp->GetReflection();
                if (info != nullptr && typeName == info->typeName)
                    return comp;
            }
            return nullptr;
        }

        // transform エントリへ root 回転を 4 要素配列で控える。 反射の Euler と別に厳密なクォータニオンを運ぶ
        void WriteRotationQuatField(nlohmann::json& transformEntry, const NS::Math::Quaternion& rotation)
        {
            transformEntry["fields"][std::string(k_RotationQuatFieldName)] =
                nlohmann::json{rotation.x, rotation.y, rotation.z, rotation.w};
        }

        // 控えの厳密なクォータニオンで root 回転を上書きする。 控えが無い読込直後や旧データは Euler のまま
        void ApplyRotationQuatOverride(GameObject& obj, const nlohmann::json& transformEntry)
        {
            const auto fieldsIt = transformEntry.find("fields");
            if (fieldsIt == transformEntry.end() || !fieldsIt->is_object())
                return;
            const auto quatIt = fieldsIt->find(std::string(k_RotationQuatFieldName));
            if (quatIt == fieldsIt->end() || !quatIt->is_array() || quatIt->size() != 4u)
                return;
            const nlohmann::json& q = *quatIt;
            if (!q[0].is_number() || !q[1].is_number() || !q[2].is_number() || !q[3].is_number())
                return;
            obj.Root().SetRotation(
                NS::Math::Quaternion{q[0].get<float>(), q[1].get<float>(), q[2].get<float>(), q[3].get<float>()});
        }
    } // namespace

    // データを唯一の正とする主経路。既定構成を積む GameObject では値だけが写り二重生成しない
    // データと live は 1 対 1 で対応させる
    void ApplyObjectComponents(GameObject& obj, const ObjectData& object, const ComponentBuiltFn& onBuilt)
    {
        if (!object.components.is_array())
            return;

        std::vector<Component*> applied;
        for (const nlohmann::json& entry : object.components)
        {
            const std::string_view typeName = ComponentEntryType(entry);
            if (typeName.empty())
                continue;

            Component* created = FindExistingComponent(obj, typeName, applied);
            if (created == nullptr)
                created = CreateComponent(std::string(typeName), obj);
            if (created == nullptr)
                continue; // 許可リスト外 / 未知の型は読み飛ばす
            applied.push_back(created);

            // data の id を実体へ焼く。以降この component は並び順でなく id で名指しできる
            ObjectIdAccess::SetId(*created, ComponentEntryId(entry));
            created->SetEnabled(ComponentEntryEnabled(entry));

            const auto fieldsIt = entry.find("fields");
            if (fieldsIt != entry.end())
                ApplyJsonFields(*created, *fieldsIt);

            // Euler の反射適用で丸まった root 回転を、 控えの厳密なクォータニオンで戻して往復ドリフトを断つ
            if (typeName == k_TransformTypeName)
                ApplyRotationQuatOverride(obj, entry);

            if (onBuilt)
                onBuilt(*created, entry);
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

        // 参照文字列の実体化は component 自身の仕事。 AssetManager が無い間は文字列のまま持たせておく
        if (assets != nullptr)
        {
            for (Component* comp : obj->Components())
            {
                if (comp != nullptr)
                    comp->ResolveAssets(*assets);
            }
        }
        return obj;
    }

    ObjectData MakeObjectData(const GameObject& obj)
    {
        ObjectData data{};
        data.className = obj.ClassName();
        data.name = obj.Name();
        data.order = obj.Order();
        data.active = obj.IsActiveSelf();
        if (const GameObject* parent = obj.Parent())
            data.parentId = parent->Id();
        for (const Component* comp : obj.Components())
        {
            if (comp == nullptr)
                continue;
            const ReflectionInfo* info = comp->GetReflection();
            if (info == nullptr)
                continue;
            data.components.push_back(MakeComponentEntry(info->typeName));
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
        data.order = obj.Order();
        data.active = obj.IsActiveSelf();
        if (const GameObject* parent = obj.Parent())
            data.parentId = parent->Id();
        for (const Component* comp : obj.Components())
        {
            if (comp == nullptr)
                continue;
            if (comp->GetReflection() == nullptr)
                continue;
            nlohmann::json entry = SerializeComponent(*comp);
            // id は往復で保つ。落とすと保存のたびに振り直しになり、名指ししている参照が外れる
            SetComponentEntryId(entry, comp->Id());
            // active はデータ側だけを写す。 モード切替の一時的な休止 (SetActive) は保存に持ち込まない
            SetComponentEntryEnabled(entry, comp->IsEnabled());
            data.components.push_back(std::move(entry));
        }
        // 回転は Euler を経由すると往復で誤差が積もるため、 root quaternion を控えて厳密なまま持ち回す
        if (nlohmann::json* transform = FindComponentEntry(data, k_TransformTypeName))
            WriteRotationQuatField(*transform, obj.Root().Rotation());
        return data;
    }
} // namespace NS::Object

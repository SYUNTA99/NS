#include "Framework/Scene/ObjectBuilder.h"

#include "Framework/Scene/Component.h"
#include "Framework/Scene/ComponentRegistry.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/Reflection.h"
#include "Framework/Scene/ReflectionJson.h"
#include "Framework/Scene/SceneData.h"
#include "Framework/Scene/SceneJson.h"

#include <algorithm>
#include <string>
#include <vector>

namespace NS::Scene
{
    namespace
    {
        // 器に既に載る同型 component を反射型名で探す。 適用済みの控えにある分は飛ばし、 無ければ nullptr
        Component* FindExistingComponent(GameObject& obj,
                                         const std::string& typeName,
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
    } // namespace

    // full SSOT 主経路: 素の器では同型が無く全生成になり、 既定構成を積む器では ctor の構成へ値だけが
    // 写って二重生成しない。 データと live は 1 対 1 で対応させる
    void ApplyObjectComponents(GameObject& obj, const ObjectData& object, const ComponentBuiltFn& onBuilt)
    {
        std::vector<Component*> applied;
        for (const auto& component : object.components)
        {
            Component* created = FindExistingComponent(obj, component.typeName, applied);
            if (created == nullptr)
                created = CreateComponent(component.typeName, obj);
            if (created == nullptr)
                continue; // 許可リスト外 / 未知 type は読み飛ばす
            applied.push_back(created);

            const nlohmann::json fields = ComponentFieldsToJson(component);
            ApplyJsonFields(*created, fields);

            if (onBuilt)
                onBuilt(*created, component);
        }
    }

    void ApplyObjectTransform(GameObject& obj, const ObjectData& object) noexcept
    {
        obj.Root().SetPosition(NS::Math::Vector3{object.positionX, object.positionY, object.positionZ});
        obj.Root().SetRotation(
            NS::Math::Quaternion{object.rotationX, object.rotationY, object.rotationZ, object.rotationW});
        obj.Root().SetScale(NS::Math::Vector3{object.scaleX, object.scaleY, object.scaleZ});
    }
} // namespace NS::Scene

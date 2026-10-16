#include "NSlib/CoreObject/Reflection/AssetRef.h"

#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/Object.h"

#include <string>

namespace NS
{
    namespace
    {
        const std::string k_NoName;
    } // namespace

    void LoadAssetField(Mesh*& field, std::string_view name)
    {
        field = AssetManager::Get().LoadMesh(name);
    }

    void LoadAssetField(Material*& field, std::string_view name)
    {
        field = AssetManager::Get().LoadMaterial(name);
    }

    const std::string& AssetFieldName(const Mesh* field) noexcept
    {
        if (field == nullptr)
        {
            return k_NoName;
        }
        return field->Path();
    }

    const std::string& AssetFieldName(const Material* field) noexcept
    {
        if (field == nullptr)
        {
            return k_NoName;
        }
        return field->Path();
    }

    void LoadAssetRef(AssetRef<const SkinnedModel>& ref)
    {
        ref.SetLoaded(AssetManager::Get().LoadSkinnedModel(ref.Name()));
    }

    void LoadAssetRef(AssetRef<const Anim::AnimClip>& ref)
    {
        ref.SetLoaded(AssetManager::Get().LoadAnimClip(ref.Name()));
    }

    void LoadAssetRefs(SceneActor& actor)
    {
        ForEachReflectedSubObj(actor, [](const SubObjectDesc&, Object* subObject) {
            const ReflectionInfo* info = subObject->GetReflection();
            if (info == nullptr)
            {
                return;
            }
            for (std::size_t i = 0; i < info->fieldCount; ++i)
            {
                const FieldDesc& field = info->fields[i];
                if (field.type == FieldType::AssetRef && field.load != nullptr)
                {
                    field.load(subObject);
                }
            }
        });
    }
} // namespace NS

#include "NSlib/CoreObject/Reflection/AssetField.h"

#include "NSlib/CoreObject/Asset/AnimClip.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/SkinnedModel.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <string>

namespace NS
{
    namespace
    {
        const std::string k_NoName;

        template <class T> const std::string& PathOrEmpty(const T* field) noexcept
        {
            if (field == nullptr)
            {
                return k_NoName;
            }
            return field->Path();
        }
    } // namespace

    void LoadAssetField(Mesh*& field, std::string_view name)
    {
        field = AssetManager::Get().LoadMesh(name);
    }

    void LoadAssetField(Material*& field, std::string_view name)
    {
        field = AssetManager::Get().LoadMaterial(name);
    }

    void LoadAssetField(SkinnedModel*& field, std::string_view name)
    {
        field = AssetManager::Get().LoadSkinnedModel(name);
    }

    void LoadAssetField(AnimClip*& field, std::string_view name)
    {
        field = AssetManager::Get().LoadAnimClip(name);
    }

    const std::string& AssetFieldName(const Mesh* field) noexcept
    {
        return PathOrEmpty(field);
    }

    const std::string& AssetFieldName(const Material* field) noexcept
    {
        return PathOrEmpty(field);
    }

    const std::string& AssetFieldName(const SkinnedModel* field) noexcept
    {
        return PathOrEmpty(field);
    }

    const std::string& AssetFieldName(const AnimClip* field) noexcept
    {
        return PathOrEmpty(field);
    }

    void LoadAssetFields(SceneActor& actor)
    {
        ForEachReflectedClassObj(actor, [](const ClassObjInfo&, Object* classObj) {
            const ReflectionInfo* info = classObj->GetReflection();
            if (info == nullptr)
            {
                return;
            }
            for (std::size_t i = 0; i < info->fieldCount; ++i)
            {
                const FieldDesc& field = info->fields[i];
                if (field.type == FieldType::Asset && field.load != nullptr)
                {
                    field.load(classObj);
                }
            }
        });
    }
} // namespace NS

#include "NSlib/CoreObject/Reflection/AssetRef.h"

#include "NSlib/CoreObject/AssetManager.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/Graphics/Material.h"
#include "NSlib/Graphics/Mesh.h"

#include <string>

namespace NS
{
    void LoadAssetRef(AssetRef<Gfx::Mesh>& ref)
    {
        ref.SetLoaded(AssetManager::Get().LoadMesh(ref.Name()));
    }

    void LoadAssetRef(AssetRef<Gfx::Material>& ref)
    {
        ref.SetLoaded(AssetManager::Get().LoadMaterial(ref.Name()));
    }

    void LoadAssetRef(AssetRef<const SkinnedModel>& ref)
    {
        ref.SetLoaded(AssetManager::Get().LoadSkinnedModel(ref.Name()));
    }

    void LoadAssetRef(AssetRef<const Gfx::AnimClip>& ref)
    {
        ref.SetLoaded(AssetManager::Get().LoadAnimClip(ref.Name()));
    }

    void LoadAssetRefs(SceneActor& actor)
    {
        ForEachReflectedSubObj(actor, [](const SubObjectDesc&, SubObject* subObject) {
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

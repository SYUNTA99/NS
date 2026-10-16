#include "NSlib/CoreObject/Reflection/AssetRef.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/AssetManager.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/Graphics/Material.h"
#include "NSlib/Graphics/Mesh.h"
#include "NSlib/Graphics/StaticMesh.h"

#include <optional>
#include <string>

namespace NS
{
    template <> void LoadAssetRef<Gfx::Mesh>(AssetRef<Gfx::Mesh>& ref)
    {
        AssetManager& assets = AssetManager::Get();
        Gfx::Mesh* mesh = ResolveMeshFromRef(assets, ref.Name());
        if (mesh == nullptr)
        {
            // 空の名前は「メッシュを選んでいない」で、誤りではない。組み込みの名前は装置が無くて登録されていない時だけ外れる
            if (!ref.Name().empty() && !AssetManager::IsBuiltinName(ref.Name()))
            {
                NS_LOG_WARN(Scene, "メッシュ {} を読めないので cube で描く", ref.Name());
            }
            mesh = assets.Builtin("cube");
        }
        ref.SetLoaded(mesh);
    }

    template <> void LoadAssetRef<Gfx::Material>(AssetRef<Gfx::Material>& ref)
    {
        // 共有のマテリアルの名前を先に引き、外れたら .mat のパスとして読む
        // 共有の名前は装置が無くて登録されていない時だけ外れる。ファイルでないので、パスとして読まない
        AssetManager& assets = AssetManager::Get();
        Gfx::Material* material = assets.SharedMaterial(ref.Name());
        if (material == nullptr && !ref.Name().empty() && !AssetManager::IsSharedMaterialName(ref.Name()))
        {
            if (const std::optional<std::string> resolved = ResolveContentPath(ref.Name()))
            {
                material = assets.LoadMaterial(*resolved).material;
            }
            if (material == nullptr)
            {
                NS_LOG_WARN(Scene, "マテリアル {} を読めないので共有の player で描く", ref.Name());
            }
        }
        if (material == nullptr)
        {
            material = assets.SharedMaterial("player");
        }
        ref.SetLoaded(material);
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

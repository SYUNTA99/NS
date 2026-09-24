#include "Editor/Undo/ObjectSnapshotApplier.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Editor
{
    std::optional<nlohmann::json> ObjectSnapshotApplier::CaptureObject(std::uint32_t objectId) const
    {
        if (m_scene == nullptr)
            return std::nullopt;

        const NS::Obj::GameObject* obj = m_scene->Objects().FindByObjectId(objectId);
        if (obj == nullptr)
            return std::nullopt;
        // undo は編集値ごと戻すため、配置物が自分を全 component 値まで忠実に書き出す
        return NS::Obj::ObjectToJson(*obj);
    }

    void ObjectSnapshotApplier::ApplyObjectSnapshot(std::uint32_t objectId, const std::optional<nlohmann::json>& desired)
    {
        if (m_scene == nullptr)
            return;

        // 対象 1 体だけを作り直す。世界ごと組み直すと、他の配置物の実行時の状態まで最初へ戻る
        if (desired)
        {
            nlohmann::json entry = *desired;
            NS::Obj::SetObjectJsonId(entry, objectId);
            (void)m_scene->ReplaceFromJson(entry);
        }
        else
        {
            m_scene->DestroyObject(objectId);
        }
        // 作り直した 1 体の当たりを張り直し、参照を引き直す側へ知らせる
        m_scene->SyncPhysics();
    }
} // namespace NS::Editor

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

        // 対象 1 体へ姿を書き戻す。構成が同じなら実体は残し、component が増減した時だけ作り直す
        if (desired)
        {
            nlohmann::json entry = *desired;
            NS::Obj::SetObjectJsonId(entry, objectId);
            (void)m_scene->ApplyFromJson(entry);
        }
        else
        {
            m_scene->DestroyObject(objectId);
        }
        // 作り直した 1 体の当たりを張り直し、参照を引き直す側へ知らせる
        m_scene->SyncPhysics();
    }
} // namespace NS::Editor

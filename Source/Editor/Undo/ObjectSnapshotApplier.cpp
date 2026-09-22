#include "Editor/Undo/ObjectSnapshotApplier.h"

#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Editor
{
    std::optional<NS::Obj::ObjectData> ObjectSnapshotApplier::CaptureObject(std::uint32_t objectId) const
    {
        if (m_scene == nullptr)
            return std::nullopt;

        for (const NS::Obj::GameObject* obj : m_scene->Objects())
        {
            if (obj->Id() != objectId)
                continue;
            // undo は編集値ごと戻すため、全 component 値を忠実に写す
            NS::Obj::ObjectData od = NS::Obj::CaptureObjectData(*obj);
            od.objectId = objectId;
            return od;
        }
        return std::nullopt;
    }

    void ObjectSnapshotApplier::ApplyObjectSnapshot(std::uint32_t objectId,
                                                    const std::optional<NS::Obj::ObjectData>& desired)
    {
        if (m_scene == nullptr)
            return;

        // 現 live をその場限りの作業データへ忠実に写し、対象 1 体だけ差し替え/新規/除去して全体を組み直す
        NS::Obj::SceneData working = m_scene->CaptureLiveToSceneData();
        const std::size_t index = NS::Obj::FindObjectIndexById(working, objectId);
        if (desired)
        {
            NS::Obj::ObjectData entry = *desired;
            entry.objectId = objectId;
            if (index != NS::Obj::k_NoObjectIndex)
                working.objects[index] = std::move(entry);
            else
                working.objects.push_back(std::move(entry));
        }
        else if (index != NS::Obj::k_NoObjectIndex)
        {
            working.objects.erase(working.objects.begin() + static_cast<std::ptrdiff_t>(index));
        }
        // 足したばかりの component の採番も、環境値の取込も、LoadFromData が中で済ませる
        m_scene->LoadFromData(std::move(working));
    }
} // namespace NS::Editor

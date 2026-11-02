#include "Editor/Undo/ActorSnapshotApplier.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    namespace Editor
    {
        std::optional<nlohmann::json> ActorSnapshotApplier::CaptureActor(uint32 objectId) const
        {
            const SceneActor* obj = m_scene.Actors().FindByActorId(objectId);
            if (obj == nullptr)
            {
                return std::nullopt;
            }
            // undo は編集値ごと戻すため、アクターが自分を全部品の値まで忠実に書き出す
            return ActorToJson(obj);
        }

        std::unique_ptr<SceneActor> ActorSnapshotApplier::ApplyActorSnapshot(uint32 objectId,
                                                                              const nlohmann::json& desired)
        {
            nlohmann::json entry = desired;
            SetActorJsonId(entry, objectId);
            std::unique_ptr<SceneActor> replaced;
            (void)m_scene.ApplyFromJson(entry, &replaced);
            return replaced;
        }

        DetachedActor ActorSnapshotApplier::DetachActor(uint32 objectId)
        {
            return m_scene.DetachActor(objectId);
        }

        void ActorSnapshotApplier::ReattachActor(DetachedActor detached)
        {
            (void)m_scene.ReattachActor(std::move(detached));
        }
    } // namespace Editor
} // namespace NS

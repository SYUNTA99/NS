#include "Editor/Undo/ActorSnapshotApplier.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Assert.h"

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
            if (m_scene == nullptr)
            {
                return std::nullopt;
            }

            const SceneActor* obj = m_scene->Actors().FindByActorId(objectId);
            if (obj == nullptr)
            {
                return std::nullopt;
            }
            // undo は編集値ごと戻すため、配置物が自分を全部品の値まで忠実に書き出す
            return ActorToJson(obj);
        }

        std::unique_ptr<SceneActor> ActorSnapshotApplier::ApplyActorSnapshot(uint32 objectId,
                                                                              const nlohmann::json& desired)
        {
            if (m_scene == nullptr)
            {
                return nullptr;
            }

            nlohmann::json entry = desired;
            SetActorJsonId(entry, objectId);
            std::unique_ptr<SceneActor> replaced;
            (void)m_scene->ApplyFromJson(entry, &replaced);
            return replaced;
        }

        DetachedActor ActorSnapshotApplier::DetachActor(uint32 objectId)
        {
            if (m_scene == nullptr)
            {
                return DetachedActor{};
            }
            return m_scene->DetachActor(objectId);
        }

        void ActorSnapshotApplier::ReattachActor(DetachedActor detached)
        {
            NS_ASSERT(App, m_scene != nullptr, "シーンの無い適用先へ、外した配置物を戻そうとしている");
            (void)m_scene->ReattachActor(std::move(detached));
        }
    } // namespace Editor
} // namespace NS

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
            return ActorToJson(*obj);
        }

        void ActorSnapshotApplier::ApplyActorSnapshot(uint32 objectId,
                                                        const std::optional<nlohmann::json>& desired)
        {
            if (m_scene == nullptr)
            {
                return;
            }

            // 対象 1 体へ姿を書き戻す。構成が同じなら実体は残し、部品が増減した時だけ作り直す
            if (desired)
            {
                nlohmann::json entry = *desired;
                SetActorJsonId(entry, objectId);
                (void)m_scene->ApplyFromJson(entry);
            }
            else
            {
                m_scene->RemoveActor(objectId);
            }
        }
    } // namespace Editor
} // namespace NS

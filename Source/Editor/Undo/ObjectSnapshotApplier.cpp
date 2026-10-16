#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    namespace Editor
    {
        std::optional<nlohmann::json> ObjectSnapshotApplier::CaptureObject(uint32 objectId) const
        {
            if (m_scene == nullptr)
            {
                return std::nullopt;
            }

            const SceneActor* obj = m_scene->Objects().FindByObjectId(objectId);
            if (obj == nullptr)
            {
                return std::nullopt;
            }
            // undo は編集値ごと戻すため、配置物が自分を全部品の値まで忠実に書き出す
            return ObjectToJson(*obj);
        }

        void ObjectSnapshotApplier::ApplyObjectSnapshot(uint32 objectId,
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
                SetObjectJsonId(entry, objectId);
                (void)m_scene->ApplyFromJson(entry);
            }
            else
            {
                m_scene->DestroyObject(objectId);
            }
            // 作り直した 1 体の当たりを張り直し、参照を引き直す側へ知らせる
            m_scene->SyncPhysics();
        }
    } // namespace Editor
} // namespace NS

#pragma once

#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ActorList.h"

#include "NSlib/Core/Types.h"
#include <optional>

namespace NS
{
    namespace Editor
    {
        //! 永続 id ごとに現在状態を取得・適用する。適用後の物理や参照の同期も実装側が行う
        class IActorSnapshotApplier
        {
        public:
            virtual ~IActorSnapshotApplier() = default;

            //! objectId の現在状態をアクターの JSON へ写す。居なければ nullopt
            [[nodiscard]] virtual std::optional<nlohmann::json> CaptureActor(uint32 objectId) const = 0;

            //! @brief objectId を desired の姿へ揃える。居なければ desired から作る
            //! @return 作り直した時に外した古いアクター。作り直さなければ空。捨てる時は呼び手が先に OnEndPlay を呼ぶ
            [[nodiscard]] virtual std::unique_ptr<SceneActor> ApplyActorSnapshot(uint32 objectId,
                                                                                 const nlohmann::json& desired) = 0;

            //! objectId を解放せずにシーンから外して返す。返した物を捨てる時は呼び手が先に OnEndPlay を呼ぶ
            [[nodiscard]] virtual DetachedActor DetachActor(uint32 objectId) = 0;

            //! DetachActor で外した物を外す前の並びの位置へ戻す
            virtual void ReattachActor(DetachedActor detached) = 0;
        };
    } // namespace Editor
} // namespace NS

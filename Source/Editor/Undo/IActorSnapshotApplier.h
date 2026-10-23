#pragma once

#include "NSlib/CoreObject/ActorJson.h"

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

            //! objectId の現在状態を配置物の JSON へ写す。居なければ nullopt
            [[nodiscard]] virtual std::optional<nlohmann::json> CaptureActor(uint32 objectId) const = 0;

            //! objectId を desired の姿へ揃える。desired 有=姿を書き戻す/新規、nullopt=除去
            virtual void ApplyActorSnapshot(uint32 objectId, const std::optional<nlohmann::json>& desired) = 0;
        };
    } // namespace Editor
} // namespace NS

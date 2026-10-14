#pragma once

#include "NSlib/Object/ObjectJson.h"

#include "NSlib/Core/Types.h"
#include <optional>

namespace NS::Editor
{
    //! 永続 id ごとに現在状態を取得・適用する。適用後の物理や参照の同期も実装側が行う
    class IObjectSnapshotApplier
    {
    public:
        virtual ~IObjectSnapshotApplier() = default;

        //! objectId の現在状態を配置物の JSON へ写す。居なければ nullopt
        [[nodiscard]] virtual std::optional<nlohmann::json> CaptureObject(NS::uint32 objectId) const = 0;

        //! objectId を desired の姿へ揃える。desired 有=姿を書き戻す/新規、nullopt=除去
        virtual void ApplyObjectSnapshot(NS::uint32 objectId, const std::optional<nlohmann::json>& desired) = 0;
    };
} // namespace NS::Editor

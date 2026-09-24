#pragma once

#include "Runtime/Object/ObjectJson.h"

#include <cstdint>
#include <optional>

// live 実体を唯一の正データとして編集するための、objectId 単位の捕捉・適用の経路
// undo コマンドはこの抽象越しに 1 体の before/after を配置物の JSON で往復させる

namespace NS::Editor
{
    //! @brief objectId 1 体の状態を配置物の JSON で捕捉・適用する経路
    //! @details live な world を持つシーンへ写す側が実装する。編集・undo はこの 2 口だけを叩く
    //! ApplyObjectSnapshot は「desired があればその 1 体を作り直して差し替え/新規、無ければ除去」の一手で、
    //! 物理・参照・カメラ等の派生状態の同期まで実装側が面倒を見る
    class IObjectSnapshotApplier
    {
    public:
        virtual ~IObjectSnapshotApplier() = default;

        //! objectId の現在状態を配置物の JSON へ写す。居なければ nullopt
        [[nodiscard]] virtual std::optional<nlohmann::json> CaptureObject(std::uint32_t objectId) const = 0;

        //! objectId を desired の姿へ揃える。desired 有=作り直して差し替え/新規、nullopt=除去
        virtual void ApplyObjectSnapshot(std::uint32_t objectId, const std::optional<nlohmann::json>& desired) = 0;
    };
} // namespace NS::Editor

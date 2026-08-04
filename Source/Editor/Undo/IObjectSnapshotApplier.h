#pragma once

#include <cstdint>
#include <optional>

// live 実体を唯一の正データとして編集するための、 objectId 単位の捕捉・適用の窓口
// undo コマンドはこの抽象越しに 1 体の before/after スナップショットを往復させる

namespace NS::Object
{
    struct ObjectData;
}

namespace NS::Editor
{
    /// @brief objectId 1 体の状態を値スナップショットで捕捉・適用する窓口
    /// @details live な world を持つシーンへ写す側が実装する。 編集・undo はこの 2 口だけを叩く
    /// ApplyObjectSnapshot は「desired があれば組み直して差し替え/新規、 無ければ除去」の一手で、
    /// 物理・参照・カメラ等の派生状態の同期まで実装側が面倒を見る
    class IObjectSnapshotApplier
    {
    public:
        virtual ~IObjectSnapshotApplier() = default;

        /// objectId の現在状態を ObjectData へ写す。 居なければ nullopt
        [[nodiscard]] virtual std::optional<NS::Object::ObjectData> CaptureObject(std::uint32_t objectId) const = 0;

        /// objectId を desired の姿へ揃える。 desired 有=組み直して差し替え/新規、 nullopt=除去
        virtual void ApplyObjectSnapshot(std::uint32_t objectId,
                                         const std::optional<NS::Object::ObjectData>& desired) = 0;
    };
} // namespace NS::Editor

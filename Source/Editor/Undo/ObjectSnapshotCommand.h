#pragma once

#include "Editor/Undo/ICommand.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <optional>

namespace NS::Editor
{

    /// @brief 永続 id で指す 1 オブジェクトを before → after のスナップショットで切り替える唯一の編集コマンド
    /// @details 追加・削除・変形・複製・材質・コンポーネント増減を全てこの 1 種で表す
    /// after 有 = その姿へ組み直して差し替え/新規、 after 無 = 除去。 Undo は before へ同じ手で戻す
    /// 派生状態 (物理・参照・カメラ) の同期は適用側の ApplyObjectSnapshot が面倒を見る
    /// 対象は objectId で再特定するので、 間に別の追加削除で並びが動いても追従する
    class ObjectSnapshotCommand final : public ICommand
    {
    public:
        /// 永続 id `id` を `after` の姿へ置くコマンド。 Undo で `before` へ戻す
        /// 追加は before 無 / after 有、 削除は before 有 / after 無、 編集は両方
        ObjectSnapshotCommand(std::uint32_t id,
                              std::optional<NS::Object::ObjectData> before,
                              std::optional<NS::Object::ObjectData> after) noexcept;

        void Do(IObjectSnapshotApplier& target) noexcept override;
        void Undo(IObjectSnapshotApplier& target) noexcept override;

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override;

    private:
        std::uint32_t m_id;                              // 対象オブジェクトの永続 id
        std::optional<NS::Object::ObjectData> m_before; // 適用前の姿 (Undo 復元用、 無ければ未存在)
        std::optional<NS::Object::ObjectData> m_after;  // 適用後の姿 (無ければ除去)
    };

} // namespace NS::Editor

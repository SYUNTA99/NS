#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <cstdint>
#include <vector>

namespace NS::Object
{
    class GameObject;
}

class LevelEditorController;

namespace NS::Editor
{
    //! @brief 配置物の親子ツリーを出すヒエラルキーパネル
    //! @details 選択・改名・親付け替え・複製/削除・範囲選択の UI 状態を自分で持ち、
    //! 実際の適用は controller へ流す。 world を跨ぐので要求は木を描き終えてから一括で流す
    class HierarchyPanel : public NS::Core::NonCopyable
    {
    public:
        //! ヒエラルキーを 1 枚描く。 選択・改名・付け替え・複製/削除の要求は controller へ渡す
        void Render(LevelEditorController& editor) noexcept;

    private:
        //! @brief 1 行を描く
        //! @param[in] withChildren 真なら子を再帰で下へ潜る。 検索中は一致した物だけを平らに並べるので偽で呼ぶ
        void RenderNode(LevelEditorController& editor, NS::Object::GameObject& object, bool withChildren) noexcept;

        //! 対象を改名待ちにして、 入力欄へ現在の表示名を入れる
        void BeginRename(NS::Object::GameObject& object) noexcept;

        std::uint32_t m_renamingObjectId = 0; // 改名中の配置物の永続 id、0 なら改名していない
        char m_renameBuffer[64]{};            // 改名の入力欄
        bool m_renameFocusPending = false;    // 入力欄を出した最初のフレームでキーボード焦点を渡す

        // 木を描いている最中に world を組み直すと一覧が入れ替わるので、要求だけ溜めて描き終えてから流す
        std::uint32_t m_renameCommitId = 0;     // このフレームに確定した改名の対象、0 なら無し
        std::uint32_t m_reparentChildId = 0;    // 付け替える子
        std::uint32_t m_reparentParentId = 0;   // 付け替え先の親、0 は root
        bool m_reparentPending = false;         // 付け替え要求が出ているか
        std::uint32_t m_expandParentId = 0;     // 付け替え先を一度だけ開く。閉じた親へ落とした子が消えて見えるのを防ぐ
        std::uint32_t m_duplicateRequestId = 0; // このフレームに複製を頼まれた対象、0 なら無し
        std::uint32_t m_deleteRequestId = 0;    // このフレームに削除を頼まれた対象、0 なら無し

        std::uint32_t m_selectionAnchorId = 0; // Shift クリックの起点
        std::uint32_t m_rangeSelectToId = 0;   // このフレームに Shift クリックされた行、0 なら無し
        bool m_focusPending = false; // このフレームに行がダブルクリックされたか。選択が確定してから視点を寄せる
        std::vector<std::uint32_t> m_visibleOrder; // ヒエラルキーに描いた行の並び。範囲選択の解決に使う

        char m_hierarchyFilter[64]{}; // ヒエラルキーの検索欄。空でない間は木を畳んで一致だけ並べる
    };
} // namespace NS::Editor

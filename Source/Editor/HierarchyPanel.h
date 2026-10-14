#pragma once

#include "NSlib/Core/NonCopyable.h"

#include "NSlib/Core/Types.h"
#include <vector>

namespace NS::Obj
{
    class Actor;
}

class LevelEditorController;

namespace NS::Editor
{
    //! 配置物の親子関係と選択を表示する。改名・親変更などの適用は描画後に行う
    class HierarchyPanel : public NS::NonCopyable
    {
    public:
        //! ヒエラルキーを 1 枚描く。選択・改名・付け替え・複製/削除の要求は controller へ渡す
        void Render(LevelEditorController& editor) noexcept;

    private:
        //! 検索中は withChildren を false にし、一致する配置物だけを平らに並べる
        void RenderNode(LevelEditorController& editor, NS::Obj::Actor& object, bool withChildren) noexcept;

        //! 対象を改名待ちにして、入力欄へ現在の表示名を入れる
        void BeginRename(NS::Obj::Actor& object) noexcept;

        NS::uint32 m_renamingObjectId = 0; // 改名中の配置物の永続 id、0 なら改名していない
        char m_renameBuffer[64]{};         // 改名の入力欄
        bool m_renameFocusPending = false; // 入力欄を出した最初のフレームでキーボード焦点を渡す

        // 木を描いている最中に world を組み直すと一覧が入れ替わるので、要求だけ溜めて描き終えてから流す
        NS::uint32 m_renameCommitId = 0;     // このフレームに確定した改名の対象、0 なら無し
        NS::uint32 m_reparentChildId = 0;    // 付け替える子
        NS::uint32 m_reparentParentId = 0;   // 付け替え先の親、0 は root
        bool m_reparentPending = false;      // 付け替え要求が出ているか
        NS::uint32 m_expandParentId = 0;     // 付け替え先を一度だけ開く。閉じた親へ落とした子が消えて見えるのを防ぐ
        NS::uint32 m_duplicateRequestId = 0; // このフレームに複製を頼まれた対象、0 なら無し
        NS::uint32 m_deleteRequestId = 0;    // このフレームに削除を頼まれた対象、0 なら無し

        NS::uint32 m_selectionAnchorId = 0; // Shift クリックの起点
        NS::uint32 m_rangeSelectToId = 0;   // このフレームに Shift クリックされた行、0 なら無し
        bool m_focusPending = false;        // このフレームに行がダブルクリックされたか。選択が確定してから視点を寄せる
        std::vector<NS::uint32> m_visibleOrder; // ヒエラルキーに描いた行の並び。範囲選択の解決に使う

        char m_hierarchyFilter[64]{}; // ヒエラルキーの検索欄。空でない間は木を畳んで一致だけ並べる
    };
} // namespace NS::Editor

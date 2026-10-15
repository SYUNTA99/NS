#pragma once

#include "NSlib/Core/NonCopyable.h"

#include "NSlib/Core/Types.h"
#include <vector>

namespace NS
{
    class SceneActor;
}

class LevelEditorController;

namespace NS
{
    namespace Editor
    {
        //! 配置物を平らな一覧で表示する。改名などの適用は描画後に行う
        class ActorListPanel : public NS::NonCopyable
        {
        public:
            //! 個体一覧を描く。選択・改名・複製・削除の要求は controller へ渡す
            void Render(LevelEditorController& editor) noexcept;

        private:
            void RenderRow(LevelEditorController& editor, NS::SceneActor& object) noexcept;

            //! 対象を改名待ちにして、入力欄へ現在の表示名を入れる
            void BeginRename(NS::SceneActor& object) noexcept;

            NS::uint32 m_renamingObjectId = 0; // 改名中の配置物の永続 id、0 なら改名していない
            char m_renameBuffer[64]{};         // 改名の入力欄
            bool m_renameFocusPending = false; // 入力欄を出した最初のフレームでキーボード焦点を渡す

            // 描画中に world を組み直すと一覧が入れ替わるので、要求だけ溜めて描き終えてから流す
            NS::uint32 m_renameCommitId = 0;     // このフレームに確定した改名の対象、0 なら無し
            NS::uint32 m_duplicateRequestId = 0; // このフレームに複製を頼まれた対象、0 なら無し
            NS::uint32 m_deleteRequestId = 0;    // このフレームに削除を頼まれた対象、0 なら無し

            NS::uint32 m_selectionAnchorId = 0; // Shift クリックの起点
            NS::uint32 m_rangeSelectToId = 0;   // このフレームに Shift クリックされた行、0 なら無し
            bool m_focusPending = false; // このフレームに行がダブルクリックされたか。選択が確定してから視点を寄せる
            std::vector<NS::uint32> m_visibleOrder; // 一覧に描いた行の並び。範囲選択の解決に使う

            char m_actorListFilter[64]{}; // 一覧の検索欄。一致する個体だけを並べる
        };
    } // namespace Editor
} // namespace NS

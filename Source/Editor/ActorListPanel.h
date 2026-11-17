#pragma once

#include "NSlib/Core/NonCopyable.h"

#include "NSlib/Core/Types.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NS
{
    class SceneActor;
}

namespace NS
{
    namespace Ed
    {
        class EditorController;

        class ActorListPanel : public NonCopyable
        {
        public:
            void Render(EditorController& editor) noexcept;

        private:
            //! 親の id から、その子のアクターを一覧の並びで引く表
            using ChildrenMap = std::unordered_map<uint32, std::vector<SceneActor*>>;

            //! 一覧の行を全部描く。子は親の下に並べる
            void RenderActorRows(EditorController& editor, const std::vector<SceneActor*>& placed) noexcept;
            //! 行の右クリックのメニュー (改名・寄る・複製・削除)
            void RenderRowMenu(EditorController& editor, SceneActor* object) noexcept;
            //! object の行と、その下に開いた子孫の行を描く。描いた id は rendered に入れ、輪でも 1 回だけ描く
            void RenderTree(EditorController& editor,
                            SceneActor* object,
                            const ChildrenMap& childrenOf,
                            std::unordered_set<uint32>& rendered) noexcept;
            //! 畳んだ行の子孫を、描いた物として rendered に入れる。入れ済みの id はたどらないので輪でも止まる
            void MarkDescendantsRendered(const std::vector<SceneActor*>& children,
                                         const ChildrenMap& childrenOf,
                                         std::unordered_set<uint32>& rendered) noexcept;
            //! 1 行を描く。呼ぶ前に行の id を積んでおく。子の行を描く木が開いて積まれた場合 true
            [[nodiscard]] bool RenderRow(EditorController& editor, SceneActor* object, bool hasChildren) noexcept;
            //! 直前に描いた項目を、行を引きずる元と、引きずった行を子として受ける先にする。parent が 0 なら親を外す先
            void HandleReparentDrag(uint32 rowId, uint32 parent, const char* label) noexcept;
            void BeginRename(SceneActor* object) noexcept;

            uint32 m_renamingActorId = 0;
            char m_renameBuffer[64]{};
            bool m_renameFocusPending = false;

            uint32 m_renameCommitId = 0;
            uint32 m_duplicateRequestId = 0;
            uint32 m_deleteRequestId = 0;
            // 一覧の行を別の行へ落とした時の付け替え。描き終えてから当てる
            bool m_reparentRequested = false;
            uint32 m_reparentChildId = 0;
            uint32 m_reparentParentId = 0;

            uint32 m_selectionAnchorId = 0;
            uint32 m_rangeSelectToId = 0;
            bool m_focusPending = false;
            std::vector<uint32> m_visibleOrder;
        };
    } // namespace Ed
} // namespace NS

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
        class ActorListPanel : public NonCopyable
        {
        public:
            void Render(LevelEditorController& editor) noexcept;

        private:
            void RenderRow(LevelEditorController& editor, SceneActor* object) noexcept;
            void BeginRename(SceneActor* object) noexcept;

            uint32 m_renamingActorId = 0;
            char m_renameBuffer[64]{};
            bool m_renameFocusPending = false;

            uint32 m_renameCommitId = 0;
            uint32 m_duplicateRequestId = 0;
            uint32 m_deleteRequestId = 0;

            uint32 m_selectionAnchorId = 0;
            uint32 m_rangeSelectToId = 0;
            bool m_focusPending = false;
            std::vector<uint32> m_visibleOrder;

            char m_actorListFilter[64]{};
        };
    } // namespace Editor
} // namespace NS

#pragma once

#include "Editor/InspectorReflection.h"
#include "NSlib/Core/NonCopyable.h"

#include "NSlib/Core/Types.h"

class LevelEditorController;

namespace NS
{
    namespace Editor
    {
        //! 選択物の名前・姿勢・部品の値を編集する。改名の適用は描画後に行う
        class InspectorPanel : public NonCopyable
        {
        public:
            //! インスペクターを 1 枚描く。名前・トランスフォーム・サブオブジェクトの編集は controller へ流す
            void Render(LevelEditorController& editor) noexcept;

        private:
            char m_nameBuffer[64]{};       // 最上部の名前欄
            uint32 m_nameId = 0;       // 名前欄が今映している対象。選択が変わったら入れ直す
            uint32 m_nameCommitId = 0; // このフレームに確定した名前の対象、0 なら無し
            uint32 m_nameEditId = 0;   // 名前を打ち始めた対象。0 なら打っていない
            char m_fieldFilter[64]{};      // 欄の検索欄。空でない間は名前に含む欄だけを並べる。選択を変えても残す
        };
    } // namespace Editor
} // namespace NS

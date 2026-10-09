#pragma once

#include "Editor/InspectorReflection.h"
#include "NSlib/Core/NonCopyable.h"

#include <cstdint>

class LevelEditorController;

namespace NS::Editor
{
    //! @brief 選択中の対象のトランスフォームとサブオブジェクト構成を編集するインスペクターパネル
    //! @details 名前欄の一時状態を自分で持つ。改名は world を組み直すので、パネルを描き終えてから流す
    class InspectorPanel : public NS::NonCopyable
    {
    public:
        //! インスペクターを 1 枚描く。名前・トランスフォーム・サブオブジェクトの編集は controller へ流す
        void Render(LevelEditorController& editor) noexcept;

    private:
        char m_nameBuffer[64]{};          // 最上部の名前欄
        std::uint32_t m_nameId = 0;       // 名前欄が今映している対象。選択が変わったら入れ直す
        std::uint32_t m_nameCommitId = 0; // このフレームに確定した名前の対象、0 なら無し
        std::uint32_t m_nameEditId = 0;   // 名前を打ち始めた対象。0 なら打っていない
        SubObjectDefaults m_defaults;     // 既定と違う欄に印を出すための比べ先
        char m_fieldFilter[64]{};         // 欄の検索欄。空でない間は名前に含む欄だけを並べる。選択を変えても残す
    };
} // namespace NS::Editor

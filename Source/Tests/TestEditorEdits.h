#pragma once

// エディタの編集を、本人の操作と同じ道で試しから行う関数

#include "Editor/EditorController.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <gtest/gtest.h>

//! Ctrl を押したまま key を押して離す。押した歩と離した歩で、エディタの固定ステップの更新を 1 回ずつ呼ぶ
inline void PressCtrlKey(NS::Ed::EditorController& editor, NS::OS::Keyboard& keyboard, NS::OS::Key key)
{
    keyboard.OnKeyDown(NS::OS::Key::Ctrl);
    keyboard.OnKeyDown(key);
    editor.Tick();
    keyboard.OnKeyUp(key);
    keyboard.OnKeyUp(NS::OS::Key::Ctrl);
    editor.Tick();
}

//! インスペクタの部品の一覧の行と同じ道で、actor の姿勢の項目 fieldName に value を書く
template <class Value>
void WritePoseField(NS::Ed::EditorController& editor,
                    NS::SceneActor* actor,
                    const char* fieldName,
                    const Value& value)
{
    NS_ASSERT(Game, actor != nullptr, "WritePoseField: 渡したアクターか部品が無い");
    const NS::ClassObjInfo* row = nullptr;
    NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& candidate, const NS::Object* part) {
        if (part == &actor->TransformObj())
        {
            row = &candidate;
        }
    });
    ASSERT_NE(row, nullptr);
    const NS::FieldDesc* field = NS::FindField(actor->TransformObj().GetReflection(), fieldName);
    ASSERT_NE(field, nullptr);
    ASSERT_EQ(field->set(&actor->TransformObj(), &value), NS::FieldWriteResult::Written);
    editor.NoteFieldEdit(actor, *row, fieldName);
}

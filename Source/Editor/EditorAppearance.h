#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

namespace NS
{
    namespace Editor
    {
        //! エディタの見た目の欄。ファイルに書かず、起動ごとにこの既定から始まる
        struct EditorAppearance
        {
            // 行の地の色。文字が読めるように薄く重ねる
            Color actorFileRowColor{0.30f, 0.45f, 0.85f, 0.16f};
            Color placementRowColor{0.35f, 0.70f, 0.35f, 0.16f};

            NS_REFLECT_BEGIN(EditorAppearance, void)
            NS_FIELD(actorFileRowColor, "actorFileRowColor", ".actor へ書く欄の地の色")
            NS_FIELD(placementRowColor, "placementRowColor", "配置物へ書く欄の地の色")
            NS_REFLECT_END_VALUE()
        };

        //! actor の部品 part の欄 field の行の地の色。書く先を決める FieldDestinationOf の答えで選ぶ
        [[nodiscard]] inline Color FieldRowColor(const EditorAppearance& appearance,
                                                 const SceneActor& actor,
                                                 const Object& part,
                                                 const FieldDesc& field) noexcept
        {
            if (FieldDestinationOf(actor, part, field) == FieldDestination::ActorFile)
            {
                return appearance.actorFileRowColor;
            }
            return appearance.placementRowColor;
        }
    } // namespace Editor
} // namespace NS

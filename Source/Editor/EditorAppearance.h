#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <optional>

namespace NS::Editor
{
        //! エディタの見た目の欄。ファイルに書かず、起動ごとにこの既定から始まる
        struct EditorAppearance
        {
            // 行の地の色。文字が読めるように薄く重ねる
            Color placementRowColor{0.30f, 0.85f, 0.60f, 0.22f};

            NS_REFLECT_BEGIN(EditorAppearance, void)
            NS_FIELD(placementRowColor, "placementRowColor", "シーンのファイルのアクターへ書く欄の地の色")
            NS_REFLECT_END_VALUE()
        };

        //! actor の欄 field の行の地の色。書く先を決める FieldDestinationOf の答えで選ぶ
        //! シーンへ書く欄だけを塗り、元と派生の .actor へ書く欄は std::nullopt
        [[nodiscard]] inline std::optional<Color> FieldRowColor(const EditorAppearance& appearance,
                                                                const SceneActor* actor,
                                                                const FieldDesc& field) noexcept
        {
            NS_ASSERT(App, actor != nullptr, "FieldRowColor: アクターが null");
            if (FieldDestinationOf(actor, field) != FieldDestination::Scene)
            {
                return std::nullopt;
            }
            return appearance.placementRowColor;
        }
} // namespace NS::Editor

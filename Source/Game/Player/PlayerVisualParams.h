#pragma once

#include "NSlib/Core/Math.h"

namespace GL
{
    namespace Level
    {
        //! 反動の矢印の見た目の調整値。ReboundPreview が欄として持つ
        //! 割り当てた向きの矢印は太く、曲げられる先の矢印は細く描く
        struct ReboundPreviewDesc
        {
            //! ピクセルの寸法の基準の高さ。太さはこの高さの画面のピクセル
            float referenceHeight = 720.0f;
            //! 当たる点から矢じりの先までの、世界の長さ。単位は m
            float assignedLength = 1.6f;
            float choiceLength = 1.0f;
            float assignedThickness = 6.0f;
            float choiceThickness = 3.0f;
            //! 矢じりの線の長さが、画面に映った矢印の長さに占める割合
            float headLengthRatio = 0.3f;
            //! 矢じりの線が軸から開く角度 (度)
            float headAngleDegrees = 30.0f;
            NS::Color assignedColor{1.0f, 1.0f, 1.0f, 0.95f};
            //! 当て方で決まる返る向きが表の向きにそろう時の、その矢印の色
            NS::Color assignedOnTableColor{1.0f, 0.9f, 0.0f, 0.95f};
            NS::Color choiceColor{1.0f, 1.0f, 1.0f, 0.5f};
        };
    } // namespace Level
} // namespace GL

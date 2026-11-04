#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

namespace GL
{
    namespace Level
    {
        //! ロックオンの枠の見た目の調整値。TargetMarker が欄として持つ
        struct TargetMarkerDesc
        {
            float referenceHeight = 720.0f;
            //! 明るい線の色と、出きった後の不透明度
            NS::Color color{245.0f / 255.0f, 247.0f / 255.0f, 1.0f, 0.9f};
            float lineThickness = 3.0f;
            float armRatio = 0.25f;
            float frameGap = 6.0f;
            float frameMinSide = 70.0f;
            float appearScale = 5.0f;
            float appearMaxSide = 465.0f;
            int appearFrames = 6;
            NS::Curve appearCurve{.keys = {NS::Curve::Key{0.0f, 0.0f},
                                           NS::Curve::Key{0.25f, 0.12f},
                                           NS::Curve::Key{0.5f, 0.38f},
                                           NS::Curve::Key{0.75f, 0.70f},
                                           NS::Curve::Key{1.0f, 1.0f}},
                                  .count = 5};
            //! 枠が出る時の色と不透明度。出きるまでに color へ寄せる
            NS::Color appearColor{1.0f, 1.0f, 1.0f, 0.35f};
            float lostScale = 0.9f;
            int lostFrames = 2;
            //! 暗い縁の色と、出きった後の不透明度。出る間は明るい線の不透明度に比例させる
            NS::Color outlineColor{12.0f / 255.0f, 20.0f / 255.0f, 36.0f / 255.0f, 0.6f};
            //! 明るい線からはみ出す片側の幅。高さ 720 の画素
            float outlineWidth = 1.0f;
        };

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
            NS::Color choiceColor{1.0f, 1.0f, 1.0f, 0.5f};
        };
    } // namespace Level
} // namespace GL

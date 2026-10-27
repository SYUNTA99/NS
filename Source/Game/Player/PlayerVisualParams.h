#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

namespace GL::Level
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

        //! 突進の矢印の見た目の調整値。SlamArrow が欄として持つ
        struct SlamArrowDesc
        {
            //! 床端の浮きを刻みの半分以内にする距離
            float groundProbeSpacing = 0.1f;
            //! 矢印を出せる床探索の深さ
            float ballGroundSearchDepth = 12.0f;
            //! 丸めの差で平らな床を分割しない許容幅
            float sameHeightTolerance = 0.001f;
            //! 極端な距離で描く板が増える上限
            int maxBandPieces = 4096;
            //! 資産の明るい帯が画像の幅に占める割合
            float bandTextureSpan = 448.0f / 512.0f;
            //! 資産の不透明な矢じりが画像に占める割合
            float headTextureSpan = 960.0f / 1024.0f;
            //! 縦に退化する直角より手前の傾き上限
            float maxHeadTiltDegrees = 80.0f;
            int growFrames = 10;
            float groundLift = 0.03f;
            float headWidth = 1.75f;
            float headDepthRatio = 0.28f;
            float headDepthMin = 1.3f;
            float headDepthMax = 2.8f;
            // 矢じりを見る角度の下限 (度)。低い視点で縦に潰れないよう、手前の端を軸に起こす
            float headMinViewDegrees = 25.0f;
            float startFade = 0.5f;
            float frontSoftness = 0.3f;
            float lateStageFrom = 1.0f / 3.0f;
            // 段の色 5 つは透明度を使わない。不透明度は帯と矢じりの欄が決める
            NS::Color earlyColor{72.0f / 255.0f, 230.0f / 255.0f, 120.0f / 255.0f};
            NS::Color lateColor{1.0f, 208.0f / 255.0f, 48.0f / 255.0f};
            NS::Color fullColor{1.0f, 64.0f / 255.0f, 56.0f / 255.0f};
            // 溜めすぎきった時の色。溜めきりの赤から溜めすぎの深さで移る。暗い床でも赤と見分けられる明るさの紫
            NS::Color overchargeColor{168.0f / 255.0f, 64.0f / 255.0f, 1.0f};
            NS::Color plainColor{224.0f / 255.0f, 232.0f / 255.0f, 242.0f / 255.0f};
            //! 矢印の暗い縁の色と不透明度
            NS::Color darkColor{12.0f / 255.0f, 20.0f / 255.0f, 36.0f / 255.0f, 1.0f};
            float bandEdgeAlpha = 0.85f;
            float bandFillAlpha = 0.30f;
            float headEdgeAlpha = 0.95f;
            float headFillAlpha = 0.85f;
            float plainBandEdgeAlpha = 0.30f;
            float plainBandFillAlpha = 0.08f;
            // 隠れた矢じりに掛ける不透明度の割合。高い相手や自機に隠れた先も薄く見せる
            float occludedHeadAlpha = 0.7f;
        };
} // namespace GL::Level

#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Reflection/Curve.h"

namespace NS::Game::Level
{
    //! ロックオンの枠の見た目の調整値。PlayerParams が欄として持ち、TargetMarker が読む
    struct TargetMarkerDesc
    {
        NS::Core::Vector3 color{245.0f / 255.0f, 247.0f / 255.0f, 1.0f};
        float lineThickness = 3.0f;
        float armRatio = 0.25f;
        float frameGap = 6.0f;
        float frameMinSide = 70.0f;
        float frameAlpha = 0.9f;
        float appearScale = 5.0f;
        float appearMaxSide = 465.0f;
        int appearFrames = 6;
        NS::Obj::Curve appearCurve{.keys = {NS::Obj::Curve::Key{0.0f, 0.0f},
                                            NS::Obj::Curve::Key{0.25f, 0.12f},
                                            NS::Obj::Curve::Key{0.5f, 0.38f},
                                            NS::Obj::Curve::Key{0.75f, 0.70f},
                                            NS::Obj::Curve::Key{1.0f, 1.0f}},
                                   .count = 5};
        NS::Core::Vector3 appearColor{1.0f, 1.0f, 1.0f};
        float appearAlpha = 0.35f;
        float lostScale = 0.9f;
        int lostFrames = 2;
        NS::Core::Vector3 outlineColor{12.0f / 255.0f, 20.0f / 255.0f, 36.0f / 255.0f};
        float outlineAlpha = 0.6f;
    };

    //! 突進の矢印の見た目の調整値。PlayerParams が欄として持ち、SlamArrow が読む
    struct SlamArrowDesc
    {
        int growFrames = 10;
        float groundLift = 0.03f;
        float headWidth = 1.75f;
        float headDepthRatio = 0.28f;
        float headDepthMin = 1.3f;
        float headDepthMax = 2.8f;
        float startFade = 0.5f;
        float frontSoftness = 0.3f;
        float lateStageFrom = 1.0f / 3.0f;
        NS::Core::Vector3 earlyColor{72.0f / 255.0f, 230.0f / 255.0f, 120.0f / 255.0f};
        NS::Core::Vector3 lateColor{1.0f, 208.0f / 255.0f, 48.0f / 255.0f};
        NS::Core::Vector3 fullColor{1.0f, 64.0f / 255.0f, 56.0f / 255.0f};
        NS::Core::Vector3 plainColor{224.0f / 255.0f, 232.0f / 255.0f, 242.0f / 255.0f};
        NS::Core::Vector3 darkColor{12.0f / 255.0f, 20.0f / 255.0f, 36.0f / 255.0f};
        float darkAlpha = 1.0f;
        float bandEdgeAlpha = 0.85f;
        float bandFillAlpha = 0.30f;
        float headEdgeAlpha = 0.95f;
        float headFillAlpha = 0.85f;
        float plainBandEdgeAlpha = 0.30f;
        float plainBandFillAlpha = 0.08f;
        float plainHeadEdgeAlpha = 0.35f;
        float plainHeadFillAlpha = 0.18f;
    };

} // namespace NS::Game::Level

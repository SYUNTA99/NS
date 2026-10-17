#pragma once

#include "Game/Level/LevelMessages.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/Gfx/Render/FrameConstants.h"

namespace GL
{
    namespace Level
    {
        //! 高さ referenceHeight 基準の画素数を、世界位置 at の奥行きで m に変換する
        //! at がカメラの後ろなら距離で測る。基準高さ・カメラの向きが不正なら 0
        [[nodiscard]] float ScreenPixelsToMeters(float pixels,
                                                 const NS::CameraPose& pose,
                                                 const NS::Vector3& at,
                                                 float referenceHeight = 720.0f) noexcept;

        //! 描画用の震えを作る。elapsedFrames は開始を 0 とする経過、root は現在の根の位置
        //! bodyLength は衝突点から最遠部までの m。0 以下なら全体が同時に震え始める
        //! 正の振幅割合を画素指定より優先する。時間外か震える期間が無ければ振幅 0
        [[nodiscard]] NS::Gfx::TremorCB MakeTremor(const TackleTremorDesc& desc,
                                                   int elapsedFrames,
                                                   const NS::Vector3& root,
                                                   float bodyLength,
                                                   const NS::CameraPose& pose) noexcept;

        //! 震えの全項目が有限の場合 true。有限でない震えを描くと形が消える
        [[nodiscard]] bool IsFiniteTremor(const NS::Gfx::TremorCB& tremor) noexcept;
    } // namespace Level
} // namespace GL

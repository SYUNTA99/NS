#pragma once

#include "Game/Level/LevelMessages.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/FrameConstants.h"
#include "NSlib/Object/SubObjects/CameraModifier.h"

namespace GL::Level
{
    //! @brief 画面の上の画素数を、カメラから見た at の奥行きでの世界の長さへ直す
    //! @details at がカメラより後ろにある時は、奥行きの代わりにカメラとの距離で測る
    //! @param[in] pixels 高さ referenceHeight 画素の画面の上の画素数
    //! @param[in] pose 遊びが読む視点
    //! @param[in] at 長さを測る世界の位置
    //! @param[in] referenceHeight 画素寸法を決めた基準の高さ
    //! @return 世界の長さ m。基準の高さかカメラの向きが不正な時は 0
    [[nodiscard]] float ScreenPixelsToMeters(float pixels,
                                             const NS::Obj::CameraPose& pose,
                                             const NS::Vector3& at,
                                             float referenceHeight = 720.0f) noexcept;

    //! @brief 衝撃の震えの、elapsedFrames フレーム目に描く所へ渡す震えを作る
    //! @details 振れ幅は割合が正なら 割合 × bodyLength ÷ 2、他は画素の欄を根の位置の奥行きで世界の長さへ直した物
    //! 衝突点から bodyLength 離れた所へ届くフレーム数で遅らせ、1 か所は 長さ − 届くフレーム数 で弱まって止める
    //! @param[in] desc 震えの形
    //! @param[in] elapsedFrames 震え始めてからのゲームのフレーム数。始まりのフレームが 0
    //! @param[in] root 持ち主の根の今の位置
    //! @param[in] bodyLength 衝突点から体の一番遠い所までの長さ (m)。0 以下なら体全体が同じフレームに震え始める
    //! @param[in] pose 遊びが読む視点
    //! @return 震え。elapsedFrames が 0 より前か長さ以降の時と、1 か所が震えるフレーム数が 0 以下の時は振れ幅 0
    [[nodiscard]] NS::Gfx::TremorCB MakeTremor(const TackleTremorDesc& desc,
                                               int elapsedFrames,
                                               const NS::Vector3& root,
                                               float bodyLength,
                                               const NS::Obj::CameraPose& pose) noexcept;
} // namespace GL::Level

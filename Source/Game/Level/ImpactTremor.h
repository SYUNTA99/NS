#pragma once

#include "Game/Level/LevelMessages.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/FrameConstants.h"
#include "Runtime/Object/Components/CameraModifier.h"

namespace NS::Game::Level
{
    //! @brief 画面の上の画素数を、カメラから見た at の奥行きでの世界の長さへ直す
    //! @details 画面の高さを 720 画素として、縦の視野角から 1 画素の長さを出す。横揺れと衝撃の震えの振れ幅が使う
    //! at がカメラより後ろにある時は、奥行きの代わりにカメラとの距離で測る
    //! @param[in] pixels 高さ 720 画素の画面の上の画素数
    //! @param[in] pose 遊びが読む視点
    //! @param[in] at 長さを測る世界の位置
    //! @return 世界の長さ (m)。カメラの向きが決まらない時は 0
    [[nodiscard]] float ScreenPixelsToMeters(float pixels,
                                             const NS::Obj::CameraPose& pose,
                                             const NS::Core::Vector3& at) noexcept;

    //! @brief 衝撃の震えの、elapsedFrames フレーム目に描く所へ渡す震えを作る
    //! @details 衝突点は desc.contactOffset を根からのずれのまま渡し、描く形と一緒に動かす
    //! 振れ幅は画素の欄を根の位置の奥行きで世界の長さへ直した物
    //! 衝突点から bodyLength 離れた所へ desc.reachFrames で届く速さで遅らせ、1 か所は desc.length − desc.reachFrames
    //! で弱まって止める。揺らす向きは画面の右と上
    //! @param[in] desc 震えの形
    //! @param[in] elapsedFrames 震え始めてからのゲームのフレーム数。始まりのフレームが 0
    //! @param[in] root 持ち主の根の今の位置
    //! @param[in] bodyLength 衝突点から体の一番遠い所までの長さ (m)。0 以下なら体全体が同じフレームに震え始める
    //! @param[in] pose 遊びが読む視点
    //! @return 震え。elapsedFrames が 0 より前か長さ以降の時と、1 か所が震えるフレーム数が 0 以下の時は振れ幅 0
    [[nodiscard]] NS::Gfx::TremorCB MakeTremor(const TackleTremorDesc& desc,
                                               int elapsedFrames,
                                               const NS::Core::Vector3& root,
                                               float bodyLength,
                                               const NS::Obj::CameraPose& pose) noexcept;
} // namespace NS::Game::Level

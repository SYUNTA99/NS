#pragma once

#include "NSlib/Object/SubObject.h"

#include <string>

class Player;

namespace GL::Player
{
    //! @brief 自機のアニメの欄を持つ部品。どのクリップを選ぶかは Player が欄を読んで決める
    //! @details クリップの名前は、Animation の部品に読み込んだクリップの名前と照らす
    class PlayerClips : public NS::Obj::SubObject
    {
    public:
        NS_REFLECT_BEGIN(PlayerClips, NS::Obj::SubObject)
        NS_REFLECT_FIELD(m_idleClip, "立ちのクリップ")
        NS_REFLECT_FIELD(m_walkClip, "歩きのクリップ")
        NS_REFLECT_FIELD(m_runClip, "走りのクリップ")
        NS_REFLECT_FIELD(m_jumpClip, "跳ぶクリップ")
        NS_REFLECT_FIELD(m_fallClip, "落ちるクリップ")
        NS_REFLECT_FIELD(m_ledgeHangClip, "ぶら下がりのクリップ")
        NS_REFLECT_FIELD(m_runBlendRatio, "走りへ移る速さの比")
        NS_REFLECT_FIELD(m_minPlaybackSpeed, "再生速度の下限")
        NS_REFLECT_END()

    private:
        friend class ::Player;
        std::string m_idleClip = "idle";
        std::string m_walkClip = "walk";
        std::string m_runClip = "run";
        std::string m_jumpClip{};
        std::string m_fallClip{};
        std::string m_ledgeHangClip{};
        float m_runBlendRatio = 0.4f;
        float m_minPlaybackSpeed = 0.5f;
    };
} // namespace GL::Player

#pragma once

#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/Object.h"

class PlayerActor;

namespace GL
{
    namespace Player
    {
        //! @brief 自機のアニメの欄を持つ部品。どのクリップを選ぶかは PlayerActor が欄を読んで決める
        //! @details クリップの欄の字面は「ContentRoot からの glTF の道筋」か「道筋#動きの名前」
        class PlayerClips : public NS::Object
        {
        public:
            NS_REFLECT_BEGIN(PlayerClips, NS::Object)
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
            friend class ::PlayerActor;
            NS::AssetRef<const NS::Anim::AnimClip> m_idleClip;
            NS::AssetRef<const NS::Anim::AnimClip> m_walkClip;
            NS::AssetRef<const NS::Anim::AnimClip> m_runClip;
            NS::AssetRef<const NS::Anim::AnimClip> m_jumpClip;
            NS::AssetRef<const NS::Anim::AnimClip> m_fallClip;
            NS::AssetRef<const NS::Anim::AnimClip> m_ledgeHangClip;
            float m_runBlendRatio = 0.4f;
            float m_minPlaybackSpeed = 0.5f;
        };
    } // namespace Player
} // namespace GL

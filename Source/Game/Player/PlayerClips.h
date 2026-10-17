#pragma once

#include "NSlib/CoreObject/Reflection/AssetField.h"
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
            //! 欄が nullptr にならないように、空の名前の中身の空のクリップで始める
            PlayerClips()
            {
                NS::LoadAssetField(m_idleClip, "");
                NS::LoadAssetField(m_walkClip, "");
                NS::LoadAssetField(m_runClip, "");
                NS::LoadAssetField(m_jumpClip, "");
                NS::LoadAssetField(m_fallClip, "");
                NS::LoadAssetField(m_ledgeHangClip, "");
            }

            NS_REFLECT_BEGIN(PlayerClips, NS::Object)
            NS_FIELD(m_idleClip, "立ちのクリップ")
            NS_FIELD(m_walkClip, "歩きのクリップ")
            NS_FIELD(m_runClip, "走りのクリップ")
            NS_FIELD(m_jumpClip, "跳ぶクリップ")
            NS_FIELD(m_fallClip, "落ちるクリップ")
            NS_FIELD(m_ledgeHangClip, "ぶら下がりのクリップ")
            NS_FIELD(m_runBlendRatio, "走りへ移る速さの比")
            NS_FIELD(m_minPlaybackSpeed, "再生速度の下限")
            NS_REFLECT_END()

        private:
            friend class ::PlayerActor;
            NS::AnimClip* m_idleClip = nullptr;
            NS::AnimClip* m_walkClip = nullptr;
            NS::AnimClip* m_runClip = nullptr;
            NS::AnimClip* m_jumpClip = nullptr;
            NS::AnimClip* m_fallClip = nullptr;
            NS::AnimClip* m_ledgeHangClip = nullptr;
            float m_runBlendRatio = 0.4f;
            float m_minPlaybackSpeed = 0.5f;
        };
    } // namespace Player
} // namespace GL

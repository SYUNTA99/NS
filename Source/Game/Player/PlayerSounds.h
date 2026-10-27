#pragma once

#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"

#include <limits>

namespace NS
{
    class IGetAudio;
} // namespace NS

namespace GL
{
    namespace Player
    {
        //! @brief 自機がどの出来事でどの音を鳴らすかを持つ部品
        //! @details 跳びと着地は出来事の受け手が頼み、閃光と同じ VisualStep の Update で鳴らす。
        //!          落下とゴールはその場で鳴らす。落下は消えた後に VisualStep が呼ばれず、ゴールは知らせが毎フレーム届くため
        class PlayerSounds : public NS::Object
        {
        public:
            //! 鳴らす名前の音を先に読む
            void Preload(const NS::IGetAudio* audio);

            //! 跳んだフレームに呼ぶ。次の Update で跳ぶ音を鳴らす。次の着地の音も鳴らせるようにする
            void RequestJump() noexcept;

            //! 足場を離れたフレームと、エアダッシュか体当たりを始めたフレームに呼ぶ。次の着地の音を鳴らせるようにする
            void AllowLand() noexcept;

            //! 着いたフレームに呼ぶ。Reset の後、まだ足場を離れていなければ頼まない。
            //! 前に着地の音を鳴らしてから、着地の音の最短の間隔の秒が経っていなければ頼まない
            void RequestLand() noexcept;

            //! 頼まれた音を鳴らし、頼みを消す。dt は前の Update からの実際の秒 (アクターの時間の速さを掛けない)
            void Update(const NS::IGetAudio* audio, float dt);

            void PlayGoal(const NS::IGetAudio* audio) const;
            void PlayFall(const NS::IGetAudio* audio) const;

            //! 頼みを捨て、次に足場を離れるまで着地の音を鳴らさない。やり直しとコースの開始の直後の着地で鳴らさないため
            void Reset() noexcept;

            //! 次の着地で着地の音を鳴らすか
            [[nodiscard]] bool IsLandAllowed() const noexcept { return m_landAllowed; }

            //! 次の Update で着地の音を鳴らすか
            [[nodiscard]] bool IsLandRequested() const noexcept { return m_landRequested; }

            NS_REFLECT_BEGIN(PlayerSounds, NS::Object)
            NS_FIELD(m_jumpSound, "跳ぶ音")
            NS_FIELD(m_jumpVolume, "跳ぶ音の音量", NS::FieldRange::Unit())
            NS_FIELD(m_landSound, "着地の音")
            NS_FIELD(m_landVolume, "着地の音の音量", NS::FieldRange::Unit())
            NS_FIELD(m_landMinInterval, "着地の音の最短の間隔", NS::FieldRange::NonNegative())
            NS_FIELD(m_goalSound, "ゴールの音")
            NS_FIELD(m_goalVolume, "ゴールの音の音量", NS::FieldRange::Unit())
            NS_FIELD(m_fallSound, "落下の音")
            NS_FIELD(m_fallVolume, "落下の音の音量", NS::FieldRange::Unit())
            NS_REFLECT_END()

        private:
            NS::SoundName m_jumpSound{"jump"};
            float m_jumpVolume = 0.6f;
            NS::SoundName m_landSound{"land"};
            float m_landVolume = 0.5f;
            float m_landMinInterval = 0.15f; // 着地の音を鳴らしてから、次を鳴らせるまでの秒
            NS::SoundName m_goalSound{"goal"};
            float m_goalVolume = 0.9f;
            NS::SoundName m_fallSound{"fall"};
            float m_fallVolume = 0.8f;

            bool m_jumpRequested = false;
            bool m_landRequested = false;
            bool m_landAllowed = false; // Reset の後、足場を離れるまで偽
            float m_sinceLandSound = std::numeric_limits<float>::infinity(); // 前に着地の音を鳴らしてからの秒
        };
    } // namespace Player
} // namespace GL

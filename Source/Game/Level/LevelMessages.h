#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/ModelShake.h"
#include "NSlib/CoreObject/Message/Message.h"
#include "NSlib/CoreObject/ActorJson.h"

#include "NSlib/Core/Types.h"
#include <optional>
#include <string>
#include <string_view>

namespace NS
{
    class SceneActor;
    class HitSensor;
    class Model;
    struct HitOwnerItem;
    struct HitOwnerRun;
} // namespace NS

namespace GL
{
    namespace Level
    {
        class TackleGhost;
    } // namespace Level
} // namespace GL

// コースの仕掛けと GameScene がやり取りする知らせ。型ごとに送る関数を置き、受け手は NS::IsMsg・NS::MsgCast で調べる
// 受け手が応じたかは送る関数の戻り値で分かる

namespace GL
{
    namespace Level
    {
        //! 受け手を即死させる知らせ。落下死の範囲がプレイヤーの体へ送る
        class MsgInstantDeath final : public NS::Message
        {
            NS_MESSAGE(MsgInstantDeath)
        };
        bool SendMsgInstantDeath(NS::HitSensor* receiver, NS::HitSensor* sender);
        //! ゴールに着いた知らせ。ゴールの範囲がプレイヤーの体へ送る
        class MsgGoal final : public NS::Message
        {
            NS_MESSAGE(MsgGoal)

        public:
            MsgGoal(float fadeOutSeconds, float fadeInSeconds) noexcept
                : m_fadeOutSeconds(fadeOutSeconds), m_fadeInSeconds(fadeInSeconds)
            {}
            [[nodiscard]] float FadeOutSeconds() const noexcept { return m_fadeOutSeconds; }
            [[nodiscard]] float FadeInSeconds() const noexcept { return m_fadeInSeconds; }

        private:
            float m_fadeOutSeconds;
            float m_fadeInSeconds;
        };
        bool SendMsgGoal(NS::HitSensor* receiver, NS::HitSensor* sender, float fadeOutSeconds, float fadeInSeconds);
        //! コースが始まった知らせ。GameScene がやり直しの前に全てのアクターへ 1 回送る
        //! 受け手は今の姿をやり直しの戻り先として控える。配置のデータは読み直さない。
        //! 追従カメラは今の初期の向きへ戻る
        class MsgCourseStart final : public NS::Message
        {
            NS_MESSAGE(MsgCourseStart)
        };
        bool SendMsgCourseStart(NS::SceneActor* receiver);
        //! コースを最初からやり直す知らせ。GameScene が全てのアクターへ送る
        //! 受け手は MsgCourseStart で控えた姿へ戻る。控えの無い物は応じなくてよい
        class MsgCourseRestart final : public NS::Message
        {
            NS_MESSAGE(MsgCourseRestart)
        };
        bool SendMsgCourseRestart(NS::SceneActor* receiver);
        //! 操作を止めるか戻す知らせ。GameScene がクリアの演出の間だけプレイヤーの操作を止める
        //! 世界は止めない。止めている間も重力とカメラは動いたまま。
        //! 受け手は入力を中立にする
        class MsgInputLock final : public NS::Message
        {
            NS_MESSAGE(MsgInputLock)

        public:
            explicit MsgInputLock(bool locked) noexcept : m_locked(locked) {}

            [[nodiscard]] bool Locked() const noexcept { return m_locked; }

        private:
            bool m_locked = false;
        };
        bool SendMsgInputLock(NS::SceneActor* receiver, bool locked);
        //! 残像。止めの間は根の左右に写しを置き、明けに止めの最後の姿を残して薄める
        struct TackleGhostDesc
        {
            NS::Vector3 axis{1.0f, 0.0f, 0.0f}; // 左右の写しを離す世界の長さ 1 の向き
            float spread = 0.0f;                // 左右の写しの最初の離れ (m)
            float sideOpacity = 0.0f;           // 左右の写しの不透明度
            int stopLength = 0;                 // 止めのフレーム数。このフレームで姿を写し取る
            int releaseFrames = 0;              // 明けの写しを残すフレーム数
            float releaseOpacity = 0.0f;        // 明けの写しの最初の不透明度

            //! frame フレーム目の左右の写しの離れと不透明度、写し取り、明けの写しの不透明度を ghost へ書く
            //! @param[in] frame 残像の何フレーム目か。始まりのフレームが 1
            //! @param[in] model 写し取る本体の Model。root は持ち主の根の今の固定フレームの世界行列
            //! @return まだ続く場合 true、明けの写しが消えて終わった場合は false
            bool WriteTo(TackleGhost* ghost, int frame, const NS::Model* model, const NS::Matrix& root) const noexcept;
        };

        //! 持ち主へ渡す項目の値。.actor に書き忘れた値は WARN を出して fallback を使う
        [[nodiscard]] float HitOwnerValue(const NS::HitOwnerItem& item, std::string_view name, float fallback);

        //! 持ち主へ渡す項目の値をフレーム数として読む。小数は 0 の側へ切り捨てる
        //! 書き忘れた値と、非数・無限・負・int に入らない値は WARN を出して fallback を使う
        [[nodiscard]] int HitOwnerFrames(const NS::HitOwnerItem& item, std::string_view name, int fallback);
    } // namespace Level
} // namespace GL

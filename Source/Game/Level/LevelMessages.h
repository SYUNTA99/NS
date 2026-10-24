#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/LaunchArc.h"
#include "NSlib/Core/AABB.h"
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

// コースの仕掛けと進行役がやり取りする知らせ。型ごとに「送る」と「調べる」を対で置く
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
        [[nodiscard]] bool IsMsgInstantDeath(const NS::Message& msg) noexcept;

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
        [[nodiscard]] bool IsMsgGoal(const NS::Message& msg) noexcept;

        //! @brief コースが始まった知らせ。進行役がやり直しの前に全てのアクターへ 1 回送る
        //! @details 受け手は今の姿をやり直しの戻り先として控える。配置のデータは読み直さない
        class MsgCourseStart final : public NS::Message
        {
            NS_MESSAGE(MsgCourseStart)
        };
        bool SendMsgCourseStart(NS::SceneActor* receiver);
        [[nodiscard]] bool IsMsgCourseStart(const NS::Message& msg) noexcept;

        //! @brief コースを最初からやり直す知らせ。進行役が全てのアクターへ送る
        //! @details 受け手は MsgCourseStart で控えた姿へ戻る。控えの無い物は応じなくてよい
        class MsgCourseRestart final : public NS::Message
        {
            NS_MESSAGE(MsgCourseRestart)
        };
        bool SendMsgCourseRestart(NS::SceneActor* receiver);
        [[nodiscard]] bool IsMsgCourseRestart(const NS::Message& msg) noexcept;

        //! @brief 操作を止めるか戻す知らせ。進行役がクリアの演出の間だけプレイヤーの操作を止める
        //! @details 世界は止めない。止めている間も重力とカメラは動いたまま。
        //! 受け手は入力を中立にし、止める時は溜めを放させずに捨てる
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
        [[nodiscard]] bool IsMsgInputLock(const NS::Message& msg) noexcept;

        //! @brief 体当たりの相手の答え。MsgAskTackleTarget の受け手が書く
        struct TackleTargetAnswer
        {
            float mass = 1.0f;                      //!< 重さ。反動と押し飛ばしの量に効く
            float toughness = 0.0f;                 //!< 耐久。破壊を許し、威力が耐久以上なら壊れる
            bool breakable = false;                 //!< 壊れる動きを持つか。偽なら破壊を許しても押し飛ばしへ回る
            bool placed = true;                     //!< 置かれているか。飛んでいる相手は食い込ませない
            NS::Vector3 position{0.0f, 0.0f, 0.0f}; //!< 根の位置 (世界座標)
            NS::AABB bounds{};                      //!< 体の外接箱 (世界座標)
            HitFace face{};                         //!< 面の赤の欄の写し。段と威力の倍率を JudgeHitFace で決める
            NS::SensorVolume body{};                //!< 体のセンサーの世界の形。面の大きさを出す
            LaunchShape launch{};
        };

        //! @brief 体当たりを受けるかを問う知らせ。体当たりの裁定が、重なった物の体のセンサーへ送る
        //! @details 受け手は体当たりを受けるなら答えを書いて応じる。応じなければ当たらなかった扱いになる
        //! 答えを書く欄を持つ問いの知らせ。種類だけの知らせと同じく、応じたかは戻り値で返す
        class MsgAskTackleTarget final : public NS::Message
        {
            NS_MESSAGE(MsgAskTackleTarget)

        public:
            explicit MsgAskTackleTarget(TackleTargetAnswer& answer) noexcept : m_answer(answer) {}

            [[nodiscard]] TackleTargetAnswer& Answer() const noexcept { return m_answer; }

        private:
            TackleTargetAnswer& m_answer;
        };
        bool SendMsgAskTackleTarget(NS::HitSensor* receiver, TackleTargetAnswer& outAnswer);
        [[nodiscard]] bool IsMsgAskTackleTarget(const NS::Message& msg) noexcept;

        //! @brief 体当たりを受けた相手へ渡す、当てた側の当たり
        //! @details 受け手は段ごとの自分の束を、触れた点と、自分と当てた側のセンサーで始める
        struct TackleHitDesc
        {
            HitTier tier = HitTier::Center;          //!< 当たりの段。受け手は段で自分の束を選ぶ
            NS::Vector3 contact{0.0f, 0.0f, 0.0f};   //!< 当てた側の体が受け手の表面に触れた世界の点
            NS::Vector3 impactDir{1.0f, 0.0f, 0.0f}; //!< 受け手の飛ぶ水平の向き。食い込みと潰れの向き
            //! 貫通の時だけ持つ止めのフレーム数。束の止めの長さを書き換え、押し勝っている側なので受け手を潰さない
            std::optional<int> stopOverride;
        };

        //! @brief 体当たりが当たった事を知らせる。当てた側の体のセンサーから、受け手の体のセンサーへ送る
        //! @details 受け手は受けた状態に入り、自分の束で止まる・食い込む・潰れる・揺れる・残像を出す
        class MsgTackleHit final : public NS::Message
        {
            NS_MESSAGE(MsgTackleHit)

        public:
            explicit MsgTackleHit(const TackleHitDesc& desc) noexcept : m_desc(desc) {}
            [[nodiscard]] const TackleHitDesc& Desc() const noexcept { return m_desc; }

        private:
            const TackleHitDesc& m_desc;
        };
        bool SendMsgTackleHit(NS::HitSensor* receiver, NS::HitSensor* sender, const TackleHitDesc& desc);
        [[nodiscard]] bool IsMsgTackleHit(const NS::Message& msg) noexcept;

        //! @brief 残像。止めの間は根の左右に写しを置き、明けに止めの最後の姿を残して薄める
        struct TackleGhostDesc
        {
            NS::Vector3 axis{1.0f, 0.0f, 0.0f}; //!< 左右の写しを離す世界の長さ 1 の向き
            float spread = 0.0f;                //!< 左右の写しの最初の離れ (m)
            float sideOpacity = 0.0f;           //!< 左右の写しの不透明度
            int stopLength = 0;                 //!< 止めのフレーム数。このフレームで姿を写し取る
            int releaseFrames = 0;              //!< 明けの写しを残すフレーム数
            float releaseOpacity = 0.0f;        //!< 明けの写しの最初の不透明度

            //! @brief frame フレーム目の左右の写しの離れと不透明度、写し取り、明けの写しの不透明度を ghost へ書く
            //! @param[in] frame 残像の何フレーム目か。始まりのフレームが 1
            //! @param[in] model 写し取る本体の Model。root は持ち主の根の今の固定フレームの世界行列
            //! @return まだ続く場合 true、明けの写しが消えて終わった場合は false
            bool WriteTo(TackleGhost* ghost, int frame, const NS::Model* model, const NS::Matrix& root) const noexcept;
        };

        //! @brief 残像の進み。明けの写しは止めの後も残るので、状態に依らず進めた回数で数える
        //! @details 始めた後の最初の Advance が 1 フレーム目。持ち主は見た目の段で Advance を呼ぶ
        struct TackleGhostRun
        {
            TackleGhostDesc desc{};
            int frame = 0;
            bool active = false;

            //! @brief 束が始めた持ち主へ渡す項目 TackleGhost の値で残像を始める
            //! @details 写しは画面の横を床に沿わせた向きへ離し、離れのピクセルは owner の今の根の所でゲームの中の
            //! 距離にする。項目の長さが 0 以下か、カメラの管理役が無い場合は始めない
            //! @param[in] owner 残像を出すアクター。事前条件: nullptr でない
            void Start(const NS::HitOwnerRun& run, const NS::SceneActor* owner);
            //! 動いていれば 1 フレーム進めて ghost へ書く。model と root は WriteTo へ渡す
            void Advance(TackleGhost* ghost, const NS::Model* model, const NS::Matrix& root) noexcept;
            //! 止めて、左右の写しと明けの写しを消す
            void Stop(TackleGhost* ghost) noexcept;
        };

        //! 持ち主へ渡す項目の値。.actor に書き忘れた値は WARN を出して fallback を使う
        [[nodiscard]] float HitOwnerValue(const NS::HitOwnerItem& item, std::string_view name, float fallback);

        //! @brief 持ち主へ渡す項目の値をフレーム数として読む。小数は 0 の側へ切り捨てる
        //! @details 書き忘れた値と、非数・無限・負・int に入らない値は WARN を出して fallback を使う
        [[nodiscard]] int HitOwnerFrames(const NS::HitOwnerItem& item, std::string_view name, int fallback);

        //! @brief 段ごとの束の名前。base[段の名前] の形で、variant は段の名前の後ろに添える
        //! @details 段の名前は真ん中・外れ。.actor の束の名前と同じ綴りにする (例 明け[真ん中・紫])
        [[nodiscard]] std::string TackleSetName(std::string_view base, HitTier tier, std::string_view variant = {});

        //! @brief 段ごとの、自機が当たる直前の状態で始める動作の名前。.actor の動作の名前と同じ綴りにする
        //! @details 動作は検知のフレームで終わり、最後のフレームが検知のフレームに当たる
        [[nodiscard]] std::string_view BeforeContactActionName(HitTier tier) noexcept;

        //! @brief 体当たりの止めが明けた時の、相手の飛び方
        struct TackleReleaseDesc
        {
            LaunchArc arc{};                //!< 飛ぶ曲線。壊れる時は使わない
            bool breaks = false;            //!< 壊れるか。破壊を許した時だけ真になる
            HitTier tier = HitTier::Center; //!< 当たりの段。尾の色が変わる
            float power = 0.0f;
            float launchScale = 0.0f; //!< 曲線の距離と高さに掛けた比。尾の長さに効く
            NS::uint32 hopSeed = 0;   //!< 外れで着地した後の跳ね方の種。何回目の当たりか
        };

        //! @brief 体当たりの止めが明けたことを知らせる
        //! @details 受け手は当たったの知らせを受けていれば自分の明けの束を出し、元の位置と形へ戻ってから
        //! 飛ぶか壊れ、床に跡を残す
        class MsgTackleRelease final : public NS::Message
        {
            NS_MESSAGE(MsgTackleRelease)

        public:
            explicit MsgTackleRelease(const TackleReleaseDesc& desc) noexcept : m_desc(desc) {}
            [[nodiscard]] const TackleReleaseDesc& Desc() const noexcept { return m_desc; }

        private:
            const TackleReleaseDesc& m_desc;
        };
        bool SendMsgTackleRelease(NS::SceneActor* receiver, const TackleReleaseDesc& desc);
        [[nodiscard]] bool IsMsgTackleRelease(const NS::Message& msg) noexcept;
    } // namespace Level
} // namespace GL

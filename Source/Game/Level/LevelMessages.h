#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/LaunchArc.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Message.h"
#include "Runtime/Object/ObjectJson.h"

namespace NS::Obj
{
    class Actor;
    class HitSensor;
} // namespace NS::Obj

// コースの仕掛けと進行役がやり取りする知らせ。型ごとに「送る」と「調べる」を対で置く
// 受け手が応じたかは送る関数の戻り値で分かる

namespace NS::Game::Level
{
    //! 受け手を即死させる知らせ。落下死の範囲がプレイヤーの体へ送る
    class MsgInstantDeath final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgInstantDeath)
    };
    bool SendMsgInstantDeath(NS::Obj::HitSensor& receiver, NS::Obj::HitSensor& sender);
    [[nodiscard]] bool IsMsgInstantDeath(const NS::Obj::Message& msg) noexcept;

    //! ゴールに着いた知らせ。ゴールの範囲がプレイヤーの体へ送る
    class MsgGoal final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgGoal)
    };
    bool SendMsgGoal(NS::Obj::HitSensor& receiver, NS::Obj::HitSensor& sender);
    [[nodiscard]] bool IsMsgGoal(const NS::Obj::Message& msg) noexcept;

    //! @brief コースを最初からやり直す知らせ。進行役が全ての配置物へ送る
    //! @details 受け手はプレイ開始時の凍結 (baseline) にある自分の姿へ戻る。凍結に無い物は応じなくてよい
    class MsgCourseRestart final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgCourseRestart)

    public:
        explicit MsgCourseRestart(const nlohmann::json& baseline) noexcept : m_baseline(baseline) {}

        //! プレイ開始時に凍結したシーンの JSON 文書
        [[nodiscard]] const nlohmann::json& Baseline() const noexcept { return m_baseline; }

    private:
        const nlohmann::json& m_baseline;
    };
    bool SendMsgCourseRestart(NS::Obj::Actor& receiver, const nlohmann::json& baseline);
    [[nodiscard]] bool IsMsgCourseRestart(const NS::Obj::Message& msg) noexcept;

    //! @brief 操作を止めるか戻す知らせ。進行役がクリアの演出の間だけプレイヤーの操作を止める
    //! @details 世界は止めない。止めている間も重力とカメラは動いたまま。
    //! 受け手は入力を中立にし、止める時は溜めを放させずに捨てる
    class MsgInputLock final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgInputLock)

    public:
        explicit MsgInputLock(bool locked) noexcept : m_locked(locked) {}

        //! 止めるなら true、戻すなら false
        [[nodiscard]] bool Locked() const noexcept { return m_locked; }

    private:
        bool m_locked = false;
    };
    bool SendMsgInputLock(NS::Obj::Actor& receiver, bool locked);
    [[nodiscard]] bool IsMsgInputLock(const NS::Obj::Message& msg) noexcept;

    //! @brief 体当たりの相手の答え。MsgAskTackleTarget の受け手が書く
    struct TackleTargetAnswer
    {
        float mass = 1.0f;                            //!< 重さ。押し飛ばしの距離と止めの長さに効く
        float toughness = 0.0f;                       //!< 耐久。破壊を許した時だけ、威力がこれ以下なら壊れる
        bool breakable = false;                       //!< 壊れる動きを持つか。偽なら破壊を許しても押し飛ばしへ回る
        bool placed = true;                           //!< 置かれているか。飛んでいる相手は食い込ませない
        NS::Core::Vector3 position{0.0f, 0.0f, 0.0f}; //!< 根の位置 (世界座標)
        NS::Core::AABB bounds{};                      //!< 体の外接箱 (世界座標)
        HitFace face{};                               //!< 面の赤の欄の写し。段と威力の倍率を JudgeHitFace で決める
        NS::Obj::SensorVolume body{};                 //!< 体のセンサーの世界の形。面の大きさを出す
    };

    //! @brief 体当たりを受けるかを問う知らせ。体当たりの裁定が、重なった物の体のセンサーへ送る
    //! @details 受け手は体当たりを受けるなら答えを書いて応じる。応じなければ当たらなかった扱いになる
    //! 答えを書く欄を持つ問いの知らせ。種類だけの知らせと同じく、応じたかは戻り値で返す
    class MsgAskTackleTarget final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgAskTackleTarget)

    public:
        explicit MsgAskTackleTarget(TackleTargetAnswer& answer) noexcept : m_answer(answer) {}

        //! 受け手が書く答え
        [[nodiscard]] TackleTargetAnswer& Answer() const noexcept { return m_answer; }

    private:
        TackleTargetAnswer& m_answer;
    };
    bool SendMsgAskTackleTarget(NS::Obj::HitSensor& receiver, TackleTargetAnswer& outAnswer);
    [[nodiscard]] bool IsMsgAskTackleTarget(const NS::Obj::Message& msg) noexcept;

    //! @brief 体当たりの止めの頭の、相手の形と動き
    struct TackleFreezeDesc
    {
        NS::Core::Vector3 impactDir{1.0f, 0.0f, 0.0f}; //!< 相手の飛ぶ水平の向き。食い込みと振動の軸
        float pushInDistance = 0.0f;                   //!< 止めの頭で飛ぶ向きへ食い込ませる距離 (m)
        float shakeAmplitude = 0.0f;                   //!< 止めの間に往復させる振れ幅 (m)
        float squashThickness = 1.0f;                  //!< 飛ぶ向きの厚みの倍率
        float squashHeight = 1.0f;                     //!< 高さの倍率
        bool squash = true;                            //!< 形を縮めるか。押し返している反発の時だけ縮める
        int stopSteps = 0;                             //!< 止めるフレーム数
    };

    //! @brief 体当たりの止めの頭を知らせる。受け手は置かれていれば食い込み、止めの間だけ往復し、形を縮める
    class MsgTackleFreeze final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgTackleFreeze)

    public:
        explicit MsgTackleFreeze(const TackleFreezeDesc& desc) noexcept : m_desc(desc) {}
        [[nodiscard]] const TackleFreezeDesc& Desc() const noexcept { return m_desc; }

    private:
        const TackleFreezeDesc& m_desc;
    };
    bool SendMsgTackleFreeze(NS::Obj::Actor& receiver, const TackleFreezeDesc& desc);
    [[nodiscard]] bool IsMsgTackleFreeze(const NS::Obj::Message& msg) noexcept;

    //! @brief 体当たりの止めが明けた時の、相手の飛び方
    struct TackleReleaseDesc
    {
        LaunchArc arc{};                //!< 飛ぶ曲線。壊れる時は使わない
        bool breaks = false;            //!< 壊れるか。破壊を許した時だけ真になる
        HitTier tier = HitTier::Center; //!< 当たりの段。尾の色が変わる
        float power = 0.0f;             //!< 最終威力
        float launchScale = 0.0f;       //!< 曲線の距離と高さに掛けた比。尾の長さに効く
    };

    //! @brief 体当たりの止めが明けたことを知らせる。受け手は元の位置と形へ戻ってから飛ぶか壊れ、床に跡を残す
    class MsgTackleRelease final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgTackleRelease)

    public:
        explicit MsgTackleRelease(const TackleReleaseDesc& desc) noexcept : m_desc(desc) {}
        [[nodiscard]] const TackleReleaseDesc& Desc() const noexcept { return m_desc; }

    private:
        const TackleReleaseDesc& m_desc;
    };
    bool SendMsgTackleRelease(NS::Obj::Actor& receiver, const TackleReleaseDesc& desc);
    [[nodiscard]] bool IsMsgTackleRelease(const NS::Obj::Message& msg) noexcept;
} // namespace NS::Game::Level

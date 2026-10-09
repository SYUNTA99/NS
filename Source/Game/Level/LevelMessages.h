#pragma once

#include "Game/Level/HitTier.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/LaunchArc.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Message.h"
#include "NSlib/Object/ObjectJson.h"

#include <cstdint>
#include <string>

namespace NS::Obj
{
    class Actor;
    class HitSensor;
    class Model;
} // namespace NS::Obj

// コースの仕掛けと進行役がやり取りする知らせ。型ごとに「送る」と「調べる」を対で置く
// 受け手が応じたかは送る関数の戻り値で分かる

namespace GL::Level
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
    bool SendMsgGoal(NS::Obj::HitSensor& receiver,
                     NS::Obj::HitSensor& sender,
                     float fadeOutSeconds,
                     float fadeInSeconds);
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
        float mass = 1.0f;                      //!< 重さ。押し飛ばしの距離と止めの長さに効く
        float toughness = 0.0f;                 //!< 耐久。破壊を許した時だけ、威力がこれ以下なら壊れる
        bool breakable = false;                 //!< 壊れる動きを持つか。偽なら破壊を許しても押し飛ばしへ回る
        bool placed = true;                     //!< 置かれているか。飛んでいる相手は食い込ませない
        NS::Vector3 position{0.0f, 0.0f, 0.0f}; //!< 根の位置 (世界座標)
        NS::AABB bounds{};                      //!< 体の外接箱 (世界座標)
        HitFace face{};                         //!< 面の赤の欄の写し。段と威力の倍率を JudgeHitFace で決める
        NS::Obj::SensorVolume body{};           //!< 体のセンサーの世界の形。面の大きさを出す
        LaunchShape launch{};                   //!< 押し飛ばされた時の飛び方。既定は無いので受け手が必ず書く
        std::string hitTimeline;                //!< 当たりのタイムラインの名前。空なら段の既定
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
        NS::Vector3 impactDir{1.0f, 0.0f, 0.0f}; //!< 相手の飛ぶ水平の向き。食い込みの向き
        float pushInDistance = 0.0f;             //!< 止めの頭で飛ぶ向きへ食い込ませる距離 (m)
        float squashThickness = 1.0f;            //!< 飛ぶ向きの厚みの倍率
        float squashHeight = 1.0f;               //!< 高さの倍率
        bool squash = true;                      //!< 形を縮めるか。押し返している反発の時だけ縮める
        int stopSteps = 0;                       //!< 止めるフレーム数
    };

    //! @brief 体当たりの止めの頭を知らせる。受け手は置かれていれば食い込み、止めの間だけ止まり、形を縮める
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

    //! @brief 止めの間の揺れの形
    enum class TackleShakeShape
    {
        Side,  //!< 左右へ振る横揺れ。BodyShakeOffset
        Depth, //!< 押す向きへ押しては戻す奥揺れ。DepthShakeOffset
    };

    //! @brief 止めの間の揺れ。受け手は描く形だけを axis の向きへ OffsetAt のずれで揺らす
    struct TackleShakeDesc
    {
        TackleShakeShape shape = TackleShakeShape::Side;
        NS::Vector3 axis{1.0f, 0.0f, 0.0f}; //!< 揺らす世界の長さ 1 の向き。横は画面の横、奥は押す向き
        float amplitude = 0.0f;             //!< 最初の振れ幅 (m)
        int length = 0;                     //!< 揺れのフレーム数
        std::uint32_t seed = 0;             //!< 振れ幅のばらつきの種
        float firstSign = 1.0f;             //!< 横の 1 フレーム目の向き
        int flipFrames = 1;                 //!< 横の左右を入れ替えるフレーム数
        int pushFrames = 1;                 //!< 奥の押し直すフレーム数
        float returnRatio = 0.0f;           //!< 奥の押してから 1 フレームごとの倍率

        //! @brief frame フレーム目の、描く形の世界のずれを返す
        //! @param[in] frame 揺れの何フレーム目か。始まりのフレームが 1
        //! @return ずれ。frame が 1 より前、横は length 以降、奥は length より後で 0 ベクトル
        [[nodiscard]] NS::Vector3 OffsetAt(int frame) const noexcept;
    };

    //! @brief 止めの間の揺れを知らせる。受け手は止めの間、知らせを受けたフレームを 1 フレーム目として揺れる
    class MsgTackleShake final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgTackleShake)

    public:
        explicit MsgTackleShake(const TackleShakeDesc& desc) noexcept : m_desc(desc) {}
        [[nodiscard]] const TackleShakeDesc& Desc() const noexcept { return m_desc; }

    private:
        const TackleShakeDesc& m_desc;
    };
    bool SendMsgTackleShake(NS::Obj::Actor& receiver, const TackleShakeDesc& desc);
    [[nodiscard]] bool IsMsgTackleShake(const NS::Obj::Message& msg) noexcept;

    //! @brief 残像。止めの間は根の左右に写しを置き、明けに止めの最後の姿を残して薄める
    struct TackleGhostDesc
    {
        NS::Vector3 axis{1.0f, 0.0f, 0.0f}; //!< 左右の写しを離す世界の長さ 1 の向き
        float spread = 0.0f;                //!< 左右の写しの最初の離れ (m)
        int stopLength = 0;                 //!< 止めのフレーム数。このフレームで姿を写し取る
        int releaseFrames = 0;              //!< 明けの写しを残すフレーム数
        float releaseOpacity = 0.0f;        //!< 明けの写しの最初の不透明度

        //! @brief frame フレーム目の左右の写しの離れ、写し取り、明けの写しの不透明度を model へ書く
        //! @param[in] frame 残像の何フレーム目か。始まりのフレームが 1
        //! @return まだ続く場合 true、明けの写しが消えて終わった場合は false
        bool WriteTo(NS::Obj::Model& model, int frame) const noexcept;
    };

    //! @brief 残像を知らせる。受け手は知らせを受けたフレームを 1 フレーム目として、止めの後も明けの写しを進める
    class MsgTackleGhost final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgTackleGhost)

    public:
        explicit MsgTackleGhost(const TackleGhostDesc& desc) noexcept : m_desc(desc) {}
        [[nodiscard]] const TackleGhostDesc& Desc() const noexcept { return m_desc; }

    private:
        const TackleGhostDesc& m_desc;
    };
    bool SendMsgTackleGhost(NS::Obj::Actor& receiver, const TackleGhostDesc& desc);
    [[nodiscard]] bool IsMsgTackleGhost(const NS::Obj::Message& msg) noexcept;

    //! @brief 衝撃の震え。受け手は長さの間、描く所の震えを毎フレーム MakeTremor で書き直す
    struct TackleTremorDesc
    {
        NS::Vector3 contactOffset{};    //!< 衝突点の、受け手の根の位置からのずれ (m)。体と一緒に動く
        float amplitudePixels = 0.0f;   //!< 振れ幅。高さ referenceHeight 画素の画面の上の画素数
        float amplitudeRatio = 0.0f;    //!< 振れ幅の体の半径に対する割合。正なら画素の欄より先に使う
        int reachFrames = 0;            //!< 衝突点から体の一番遠い所へ届くまでのフレーム数
        int length = 0;                 //!< 震えのフレーム数
        float referenceHeight = 720.0f; //!< 画素寸法を決めた基準の画面の高さ
    };

    //! @brief 衝撃の震えを知らせる。受け手は知らせを受けたフレームを 0 フレーム目として震える
    class MsgTackleTremor final : public NS::Obj::Message
    {
        NS_MESSAGE(MsgTackleTremor)

    public:
        explicit MsgTackleTremor(const TackleTremorDesc& desc) noexcept : m_desc(desc) {}
        [[nodiscard]] const TackleTremorDesc& Desc() const noexcept { return m_desc; }

    private:
        const TackleTremorDesc& m_desc;
    };
    bool SendMsgTackleTremor(NS::Obj::Actor& receiver, const TackleTremorDesc& desc);
    [[nodiscard]] bool IsMsgTackleTremor(const NS::Obj::Message& msg) noexcept;

    //! @brief 体当たりの止めが明けた時の、相手の飛び方
    struct TackleReleaseDesc
    {
        LaunchArc arc{};                //!< 飛ぶ曲線。壊れる時は使わない
        bool breaks = false;            //!< 壊れるか。破壊を許した時だけ真になる
        HitTier tier = HitTier::Center; //!< 当たりの段。尾の色が変わる
        float power = 0.0f;             //!< 最終威力
        float launchScale = 0.0f;       //!< 曲線の距離と高さに掛けた比。尾の長さに効く
        std::uint32_t hopSeed = 0;      //!< 外れで着地した後の跳ね方の種。何回目の当たりか
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
} // namespace GL::Level

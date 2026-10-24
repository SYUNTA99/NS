#pragma once

#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/IUse/IUseState.h"
#include "NSlib/CoreObject/Object.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    //! @brief 動作の事象 1 つ。項目の型は名前の束と同じで、状態に入ったフレームを 0 にしたフレーム数で始まりと長さを持つ
    struct ActorActionEvent
    {
        HitReactionItem item{};
        int start = 0;  //!< 始まりのフレーム。0 以上
        int length = 1; //!< 長さ (フレーム数)。0 以上
    };

    //! @brief 動作の中でセンサーを入り切りする事象
    //! @details 始まりのフレームに valid の側へ切り替え、始まり + 長さのフレームに逆へ戻す。
    //! 長さの途中で動作が止まった時も戻す
    struct ActionSensorEvent
    {
        bool attack = false; //!< 攻撃のセンサーを切り替える場合 true。偽なら体のセンサー
        bool valid = true;   //!< 長さの間、調べる対象にする場合 true。偽なら長さの間外す
        int start = 0;       //!< 始まりのフレーム。0 以上
        int length = 1;      //!< 長さ (フレーム数)。0 以上
    };

    //! @brief 状態ごとの動作。同じフレームに始まる事象は並びの順に起きる
    struct ActorAction
    {
        std::string name;
        std::vector<ActorActionEvent> events;
        std::vector<ActionSensorEvent> sensors;
    };

    //! @brief 動作 1 つを JSON の形 {"name", "events": [{"type", "start", "length", "fields"}],
    //! "sensors": [{"attack", "valid", "start", "length"}]} から読む
    //! @details 欠けた欄は既定のまま、知らない欄は WARN を出して飛ばす。sensors は無ければ空
    //! @return 名前が空・始まりか長さが負・知らない種類・入り切りの欄が真偽でない場合は std::nullopt で、
    //! error に理由を書く
    [[nodiscard]] std::optional<ActorAction> ParseActorAction(const nlohmann::json& doc, std::string& error);

    [[nodiscard]] nlohmann::json ActorActionToJson(const ActorAction& action);

    //! @brief .actor の文書の動作の並びを読む
    //! @details 壊れた動作と、前の動作と名前の重なる動作は、理由を ERROR に出して読まない
    //! @param[in] actorFileName ERROR に出す .actor の名前
    [[nodiscard]] std::vector<ActorAction> ReadActorActions(std::string_view actorFileName,
                                                            const nlohmann::json& actorFile);

    //! @brief .actor の文書へ動作の並びを書く。書く物が無ければ鍵ごと消す
    //! @details 今の並びのうち ReadActorActions が読まない動作は、字面のまま同じ位置に残す。読む動作の位置へ
    //! actions を並びの順に書き、余った actions は末尾へ足し、足りない位置は消す。今の並びが配列でなければ
    //! actions だけを書く
    void WriteActorActions(nlohmann::json& actorFile, const std::vector<ActorAction>& actions);

    //! 動作の長さ (フレーム数)。一番遅く終わる事象と入り切りの、始まり + 長さ。どちらも無ければ 0。
    //! int を越える長さは int の最大
    [[nodiscard]] int ActionLength(const ActorAction& action) noexcept;

    //! @brief 動作を始める・進める 1 回の呼び出しに渡す、持ち主の読む物と書く先。呼び出しの中だけ使い、覚えない
    //! @details 書く先は持ち主が持たなければ nullptr
    struct ActionTimelineSelf
    {
        const SceneActor* actor = nullptr;  //!< .actor の名前と状態機械を読む。事前条件: nullptr でない
        HitReaction* reaction = nullptr;    //!< 項目の事象を始める
        Model* model = nullptr;             //!< 項目が形・揺れ・震えを書く
        HitSensor* bodySensor = nullptr;    //!< 入り切りの事象が切り替える体のセンサー
        HitSensor* attackSensor = nullptr;  //!< 入り切りの事象が切り替える攻撃のセンサー
    };

    //! @brief 持ち主の .actor の動作を持ち、状態に入る時に始めた動作を、状態の歩を時計にして進める部品
    //! @details 時計は持ち主の状態に入ってから進めたフレーム数で、止めの間は状態と一緒に止まる。
    //! 始めた時と違う状態へ移ると止まり、残りの事象は起きない。項目は持ち主の HitReaction が始める。
    //! 持ち主は覚えず、呼ぶたびに受ける。進めるのは持ち主の SceneActor::Update だけ
    class ActionTimeline : public Object
    {
    public:
        //! @brief 持ち主の .actor (SceneActor::ActorFileName) から動作を読み直す。走っている動作は止める
        //! @details .actor が無ければ動作は空。壊れた動作は ReadActorActions が ERROR を出して読まない
        //! @param[in] owner 持ち主。事前条件: nullptr でない
        void ReloadActions(const SceneActor* owner);

        //! @brief 名前の動作を、持ち主の今の状態に結んで始める。状態に入る時 (OnEnter) に呼ぶ
        //! @details 走っている動作は止めてから始める。状態の最初の歩が startFrame に当たり、
        //! それより前に始まる事象は起きない。ただし startFrame の途中にある形は、startFrame の姿から描く
        //! (形は動作のフレームに合わせて描く姿で、始まりのフレームに起こす事象と違う)。
        //! 項目の向きと位置は HitReaction::Start と同じく self・other・position から出す
        //! @param[in] startFrame 状態の最初の歩に当てる動作のフレーム。事前条件: 0 以上
        //! @param[in] self 持ち主のセンサー。事前条件: nullptr でない
        //! @param[in] other 相手のセンサー。事前条件: nullptr でない
        //! @return 始めた場合 true。その名前の動作が無い場合は WARN を出して false
        bool Start(const ActionTimelineSelf& owner,
                   std::string_view name,
                   int startFrame,
                   const Vector3& position,
                   const HitSensor* self,
                   const HitSensor* other);

        //! 持ち主の .actor の名前の動作。無ければ nullptr
        [[nodiscard]] const ActorAction* FindAction(std::string_view name) const noexcept;

        //! 走っている動作を止める。入り切りの途中のセンサーは戻す
        void Stop() noexcept;

        //! @brief 持ち主の状態の歩で 1 フレーム進め、その歩に始まる事象を起こす
        //! @details 呼ぶのは SceneActor::Update の StateStep の直後だけ。状態の歩が進んでいなければ何もしない
        void Advance(const ActionTimelineSelf& owner);

        [[nodiscard]] bool IsRunning() const noexcept { return m_current != k_NoAction; }
        //! 走っている動作の名前。走っていなければ空
        [[nodiscard]] std::string_view CurrentName() const noexcept;
        //! 最後に進めた動作のフレーム。始めてから進めていなければ startFrame - 1
        [[nodiscard]] int Frame() const noexcept { return m_frame; }
        //! 最後の Advance で始まった事象の、動作の events の並びでの番号。始まった順
        [[nodiscard]] const std::vector<std::size_t>& StartedEvents() const noexcept { return m_started; }

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(ActionTimeline, Object)

    private:
        static constexpr std::size_t k_NoAction = static_cast<std::size_t>(-1);

        // 入り切りの途中の事象と、切り替えたセンサー
        struct SensorSwitch
        {
            std::size_t index = 0;      // 動作の sensors の番号
            HitSensor* sensor = nullptr; // 持ち主のセンサー。持ち主と同じだけ生きる
        };

        // 名前の動作の m_actions の番号。無ければ k_NoAction
        [[nodiscard]] std::size_t IndexOf(std::string_view name) const noexcept;
        // 入り切りの途中の物を戻す。endingFrame を渡すと、その歩に終わる物だけを戻す
        void EndSensorSwitches(std::optional<int> endingFrame) noexcept;

        std::vector<ActorAction> m_actions;  // 持ち主の .actor の動作
        std::size_t m_current = k_NoAction;  // 走っている動作の m_actions の番号
        StateId m_state = nullptr;           // 動作を始めた時の持ち主の状態
        int m_startFrame = 0;                // 状態の最初の歩に当てるフレーム
        int m_frame = -1;                    // 最後に進めたフレーム
        Vector3 m_position{};                // 始めた時に渡した位置
        const HitSensor* m_self = nullptr;   // 持ち主のセンサー。持ち主と同じだけ生きる
        const HitSensor* m_other = nullptr;  // 相手のセンサー
        std::vector<std::size_t> m_started;  // 最後の Advance で始まった事象
        std::vector<SensorSwitch> m_switched; // 入り切りの途中の事象
    };
} // namespace NS

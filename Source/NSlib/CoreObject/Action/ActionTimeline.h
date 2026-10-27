#pragma once

#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/IGet/IGetState.h"
#include "NSlib/CoreObject/Object.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace NS
{
    class SceneActor;

    //! 動作を始める・進める 1 回の呼び出しに渡す、持ち主の読む物と書く先。呼び出しの中だけ使い、覚えない
    //! 書く先は持ち主が持たなければ nullptr
    struct ActionTimelineSelf
    {
        const SceneActor* actor = nullptr;  // .actor の名前と状態機械を読む。事前条件: nullptr でない
        HitReaction* reaction = nullptr;    // 項目の事象を始める
        Model* model = nullptr;             // 項目が形・揺れ・震えを書く
        HitSensor* bodySensor = nullptr;    // 入り切りの事象が切り替える体のセンサー
        HitSensor* attackSensor = nullptr;  // 入り切りの事象が切り替える攻撃のセンサー
    };

    //! 持ち主の .actor の動作を持ち、状態に入る時に始めた動作を、状態の歩を時計にして進める部品
    //! 時計は持ち主の状態に入ってから進めたフレーム数で、止めの間は状態と一緒に止まる。
    //! 始めた時と違う状態へ移ると止まり、残りの事象は起きない。項目は持ち主の HitReaction が始める。
    //! 持ち主は覚えず、呼ぶたびに受ける。進めるのは持ち主の SceneActor::Update だけ
    class ActionTimeline : public Object
    {
    public:
        //! 持ち主の .actor (SceneActor::ActorFileName) から動作を読み直す。走っている動作は止める
        //! .actor が無ければ動作は空。壊れた動作は ReadActorActions が ERROR を出して読まない
        //! @param[in] owner 持ち主。事前条件: nullptr でない
        void ReloadActions(const SceneActor* owner);

        //! 名前の動作を、持ち主の今の状態に結んで始める。状態に入る時 (OnEnter) に呼ぶ
        //! 走っている動作は止めてから始める。状態の最初の歩が startFrame に当たり、
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

        //! 持ち主の状態の歩で 1 フレーム進め、その歩に始まる事象を起こす
        //! 呼ぶのは SceneActor::Update の状態機械の 1 歩の直後だけ。状態の歩が進んでいなければ何もしない
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
        friend bool IsStateEnd(const SceneActor* actor) noexcept;

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
        // IsStateEnd が読む、最後に始めた動作の状態に入った回数 (EntryCount) と length の控え。
        // Stop と ReloadActions では消さず、次の Start で上書きする。動作が途中で止まっても、状態は始めた時の
        // 長さで終わる。入り直すと回数が変わるので、Start を呼ばずに入り直した状態は前の長さで終わらない
        uint32 m_endEntry = 0;
        int m_endLength = 0;
        int m_startFrame = 0;                // 状態の最初の歩に当てるフレーム
        int m_frame = -1;                    // 最後に進めたフレーム
        Vector3 m_position{};                // 始めた時に渡した位置
        const HitSensor* m_self = nullptr;   // 持ち主のセンサー。持ち主と同じだけ生きる
        const HitSensor* m_other = nullptr;  // 相手のセンサー
        std::vector<std::size_t> m_started;  // 最後の Advance で始まった事象
        std::vector<SensorSwitch> m_switched; // 入り切りの途中の事象
    };

    //! actor の今の状態が、結んだ動作の長さの回数だけ OnStep を進めた場合 true。状態の OnStep で問う
    //! 最後に Start で動作を始めた後に今の状態へ入り直しておらず、その時の length が 1 以上で、今の状態の
    //! GetStateStepCount が length 以上の時に true。動作が途中で止まっても (Stop・ReloadActions) 答えは変わらない。
    //! 同じ状態の中で別の動作を Start すると、結んだ長さはその動作の長さに替わる。
    //! 移る予約の間・Start を呼ばずに入り直した状態・長さを決めていない動作・始まらなかった動作・動作の無いアクターは
    //! false
    //! @param[in] actor 事前条件: nullptr でない
    [[nodiscard]] bool IsStateEnd(const SceneActor* actor) noexcept;
} // namespace NS

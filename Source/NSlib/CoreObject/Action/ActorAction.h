#pragma once

#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Action/ActorActionInfo.h"
#include "NSlib/CoreObject/IGet/IGetState.h"
#include "NSlib/CoreObject/Object.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    class SceneActor;

    //! 動作の時計を進める 1 回の呼び出しに渡す値。呼び出しの中だけ使い、覚えない
    struct ActionClock
    {
        const IStateMachine* machine = nullptr; // 持ち主の状態機械。持たなければ nullptr
        // 持ち主の Animation の今のクリップと時刻 (秒) とループの回数。結ばない動作の時計を進める。
        // Animation を持たない・クリップを流していない時は clip が nullptr
        const Anim::AnimClip* clip = nullptr;
        float clipTime = 0.0f;
        uint32 clipLoops = 0;
    };

    //! 動作の中でセンサーの入り切りを変える頼み。持ち主が名前のセンサーへ当てる
    struct ActionSensorChange
    {
        std::string sensor; // センサーの名前 (HitSensor::Name)
        bool valid = true;  // 調べる対象にする場合 true。偽なら外す
    };

    //! 持ち主の .actor の動作を持ち、名前で始めた動作の時計を進めて、その歩に始まる事象を記録する部品
    //! 時計は 2 つ。StartWithState で始めた動作は持ち主の状態の歩数で進み、止めの間は状態と一緒に止まり、
    //! 始めた時と違う状態へ移ると止まる。Start で始めた動作は、フレーム 0 の後に BindClip で結んだ持ち主の
    //! アニメーションのクリップの時刻で進み、クリップが 1 周するとフレーム 0 からもう一度起こす。
    //! ほかの部品を知らない。始まった事象とセンサーの入り切りは持ち主の SceneActor が読み、自分の部品へ頼む。
    //! 進めるのは持ち主の SceneActor::Update だけ
    class ActorAction : public Object
    {
    public:
        //! 持ち主の .actor の動作を読み直す。走っている動作は止める
        //! 壊れた動作は ReadActorActions が ERROR を出して読まない
        //! @param[in] actorFileName 持ち主の .actor の名前 (SceneActor::ActorFileName)
        void ReloadActions(std::string_view actorFileName);

        //! 名前の動作を始める。走っている動作は止めてから始める。BindClip で結ぶまではフレーム 0 から進まない
        //! @return 始めた場合 true。その名前の動作が無い場合は false
        bool Start(std::string_view name);

        //! Start で始めた動作を clock のクリップに結ぶ。持ち主がフレーム 0 の事象を当てた直後に呼ぶ
        //! 以後はそのクリップの時刻で進み、ループの回数が増えるとフレーム 0 から事象をもう一度起こす。
        //! 結んだ時のクリップの時刻より前のフレームは起こさない。同じクリップが頭から流れ直すと、フレーム 0 から
        //! 起こし直す。違うクリップへ替わると止まる。クリップを結ばなかった (clock.clip が nullptr) か長さが 0 なら、
        //! フレーム 0 のまま進まない。状態に結んだ動作では何もしない
        void BindClip(const ActionClock& clock) noexcept;

        //! 名前の動作を持ち主の今の状態に結んで始め、状態の歩数で進める。状態に入る時 (OnEnter) に呼ぶ
        //! 走っている動作は止めてから始める。状態の最初の歩がフレーム 0 に当たる
        //! @return 始めた場合 true。その名前の動作が無い場合は false
        bool StartWithState(std::string_view name, const IStateMachine& machine);

        //! 持ち主の .actor の動作の並び
        [[nodiscard]] const std::vector<ActorActionInfo>& Actions() const noexcept { return m_actions; }

        //! 持ち主の .actor の名前の動作。無ければ nullptr
        [[nodiscard]] const ActorActionInfo* FindAction(std::string_view name) const noexcept;

        //! 走っている動作を止める。入り切りの途中のセンサーを戻す頼みを SensorChanges に足す
        void Stop();

        //! 時計を 1 フレーム進め、その歩に始まる事象とセンサーの入り切りを記録する
        //! 呼ぶのは持ち主の SceneActor だけ。Update の状態機械の 1 歩の直後と、StartAction の中 (フレーム 0 を
        //! 起こす) で呼ぶ。時計が進んでいなければ何も記録しない
        void Advance(const ActionClock& clock);

        [[nodiscard]] bool IsRunning() const noexcept { return m_current != k_NoAction; }
        //! 走っている動作の名前。走っていなければ空
        [[nodiscard]] std::string_view CurrentName() const noexcept;
        //! 走っている動作。走っていなければ nullptr
        [[nodiscard]] const ActorActionInfo* CurrentAction() const noexcept;
        //! 最後に進めた動作のフレーム。始めてから進めていなければ -1
        [[nodiscard]] int Frame() const noexcept { return m_frame; }
        //! 最後の Advance で始まった事象の、動作の events の並びでの番号。始まった順
        [[nodiscard]] const std::vector<std::size_t>& StartedEvents() const noexcept { return m_started; }
        //! まだ持ち主が当てていないセンサーの入り切りの頼みを返して空にする。並びの順に当てる
        [[nodiscard]] std::vector<ActionSensorChange> TakeSensorChanges();

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(ActorAction, Object)

    private:
        friend bool IsStateEnd(const SceneActor* actor) noexcept;

        static constexpr std::size_t k_NoAction = static_cast<std::size_t>(-1);

        // 名前の動作の m_actions の番号。無ければ k_NoAction
        [[nodiscard]] std::size_t IndexOf(std::string_view name) const noexcept;
        // 前の動作を止め、名前の動作を走らせる。無ければ false
        [[nodiscard]] bool Begin(std::string_view name);
        // 時計の今のフレーム。進んでいなければ std::nullopt
        [[nodiscard]] std::optional<int> ClockFrame(const ActionClock& clock) const noexcept;
        // from から to までのフレームに始まる事象とセンサーの入り切りを記録する
        void RunFrames(int from, int to);
        // 結んだクリップの時刻が長さより前に始まる最後のフレーム。結んだクリップがある時だけ呼ぶ
        [[nodiscard]] int LastClipFrame() const noexcept;
        // 入り切りの途中の物を戻す頼みを足す。endingFrame を渡すと、その歩に終わる物だけを戻す
        void EndSensorSwitches(std::optional<int> endingFrame);

        std::vector<ActorActionInfo> m_actions; // 持ち主の .actor の動作
        std::size_t m_current = k_NoAction;     // 走っている動作の m_actions の番号
        StateId m_state = nullptr;              // 状態に結んだ動作の、始めた時の持ち主の状態。結ばない動作は nullptr
        // 結ばない動作を結んだクリップと、結んだ後に読んだループの回数。結ぶまで m_clipBound は偽
        const Anim::AnimClip* m_clip = nullptr;
        uint32 m_clipLoops = 0;
        bool m_clipBound = false;
        // IsStateEnd が読む、最後に状態に結んで始めた動作の状態に入った回数 (EntryCount) と length の控え。
        // Stop と ReloadActions では消さず、次の StartWithState で上書きする。動作が途中で止まっても、状態は
        // 始めた時の長さで終わる。入り直すと回数が変わるので、始め直さずに入り直した状態は前の長さで終わらない
        uint32 m_endEntry = 0;
        int m_endLength = 0;
        int m_frame = -1;                                // 最後に進めたフレーム
        std::vector<std::size_t> m_started;              // 最後の Advance で始まった事象
        std::vector<std::size_t> m_switched;             // 入り切りの途中の事象の、動作の sensors の番号
        std::vector<ActionSensorChange> m_sensorChanges; // 持ち主がまだ当てていない入り切りの頼み
    };

    //! actor の今の状態が、結んだ動作の長さの回数だけ OnStep を進めた場合 true。状態の OnStep で問う
    //! 最後に StartWithState で動作を始めた後に今の状態へ入り直しておらず、その時の length が 1 以上で、今の状態の
    //! GetStateStepCount が length 以上の時に true。動作が途中で止まっても (Stop・ReloadActions) 答えは変わらない。
    //! 同じ状態の中で別の動作を StartWithState で始めると、結んだ長さはその動作の長さに替わる。
    //! 移る予約の間・始め直さずに入り直した状態・長さを決めていない動作・始まらなかった動作・動作の無いアクターは
    //! false
    //! @param[in] actor 事前条件: nullptr でない
    [[nodiscard]] bool IsStateEnd(const SceneActor* actor) noexcept;
} // namespace NS

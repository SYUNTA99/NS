#pragma once

namespace NS
{
    /// 1 固定ステップの並び。Scene::OnUpdate が書いた順に回す
    /// アクターは段を選ばない。Actors で全部のアクターを一覧の順に Update し、RenderPrep で全部の
    /// アクターの描く支度をする。管理役は Scene::AddSystemTick で段を決め、その段のアクターの後に回る
    enum class UpdatePhase
    {
        Actors,
        Physics, // 管理役が Jolt の broadphase の木を組み直す段。アクターが動かした後に組み直す
        Sensors,
        Course,
        Camera,
        UI,
        RenderPrep,
        Effects,
    };

    /// 段の時計。世界の速さ (Scene::SetWorldSpeed) が 1 未満の間に、段を間引くか
    enum class PhaseClock
    {
        World,    // 世界の速さに従い、世界を進める歩だけ回る
        RealTime, // 世界の速さに依らず毎歩回る
    };

    /// 段の時計を返す。世界の速さに従うかの表はここ 1 か所で、部品ごとに止め方を書かない
    /// UI は遅い世界でも普段の速さで動く
    [[nodiscard]] constexpr PhaseClock ClockOf(UpdatePhase phase) noexcept
    {
        switch (phase)
        {
        case UpdatePhase::UI:
            return PhaseClock::RealTime;
        default:
            return PhaseClock::World;
        }
    }

    /// ほかを止めている間 (Scene::HoldOthers) も段を回す場合 true、止める場合は false
    /// 表はここ 1 か所。Actors は回すが、Update するのは止め始めたアクターだけ。管理役は止め始めた
    /// アクターを見せる側 (カメラ・UI・描く支度) を回し、物理・判定・コースの進行・エフェクトを止める。
    /// カメラまで止めると、進み続ける止め始めたアクターが画面の中で動いて見える
    [[nodiscard]] constexpr bool RunsWhileOthersHeld(UpdatePhase phase) noexcept
    {
        switch (phase)
        {
        case UpdatePhase::Actors:
        case UpdatePhase::Camera:
        case UpdatePhase::UI:
        case UpdatePhase::RenderPrep:
            return true;
        default:
            return false;
        }
    }
} // namespace NS

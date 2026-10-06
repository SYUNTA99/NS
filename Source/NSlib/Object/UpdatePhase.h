#pragma once

namespace NS::Obj
{
    //! @brief 1 固定ステップの更新の段。Scene::OnUpdate が書いた順に回す
    //! @details Actor は Actor::Phase で自分の段を答え、部品でない物は ObjectList::AddTicker で段を決める
    enum class UpdatePhase
    {
        Input,
        Player,
        Enemy,
        Physics,
        Sensors,
        Triggers,
        Course,
        Camera,
        UI,
        RenderPrep,
        Effects,
    };

    //! @brief 段の時計。世界の速さ (Scene::SetWorldSpeed) が 1 未満の間に、段を間引くか
    enum class PhaseClock
    {
        World,    //!< 世界の速さに従い、世界を進める歩だけ回る
        RealTime, //!< 世界の速さに依らず毎歩回る
    };

    //! @brief 段の時計を返す。世界の速さに従うかの表はここ 1 か所で、部品ごとに止め方を書かない
    //! @details 入力の段は押しを溜めて次に世界を進める歩の自機へ渡すので、毎歩回す。UI は遅い世界でも普段の速さで動く
    [[nodiscard]] constexpr PhaseClock ClockOf(UpdatePhase phase) noexcept
    {
        switch (phase)
        {
        case UpdatePhase::Input:
        case UpdatePhase::UI:
            return PhaseClock::RealTime;
        default:
            return PhaseClock::World;
        }
    }

    //! @brief 自機以外の止め (Scene::HoldOthers) の間も段を回す場合 true、止める場合は false
    //! @details 表はここ 1 か所。回すのは自機と、自機を見せる側 (入力・カメラ・UI・描く支度)。
    //! 止めるのは置物・敵・物理・判定・コースの進行・エフェクト。カメラまで止めると、進み続ける自機が画面の中で動いて見える
    [[nodiscard]] constexpr bool RunsWhileOthersHeld(UpdatePhase phase) noexcept
    {
        switch (phase)
        {
        case UpdatePhase::Input:
        case UpdatePhase::Player:
        case UpdatePhase::Camera:
        case UpdatePhase::UI:
        case UpdatePhase::RenderPrep:
            return true;
        default:
            return false;
        }
    }
} // namespace NS::Obj

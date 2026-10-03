#pragma once

#include <array>

namespace NS::Obj
{
    //! @brief 1 固定ステップの更新の段。Scene::OnUpdate が k_UpdatePhases の順に回す
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

    //! 段を回す順。段を足す・並べ替える時はここと UpdatePhase を一緒に直す
    inline constexpr std::array k_UpdatePhases{UpdatePhase::Input,
                                               UpdatePhase::Player,
                                               UpdatePhase::Enemy,
                                               UpdatePhase::Physics,
                                               UpdatePhase::Sensors,
                                               UpdatePhase::Triggers,
                                               UpdatePhase::Course,
                                               UpdatePhase::Camera,
                                               UpdatePhase::UI,
                                               UpdatePhase::RenderPrep,
                                               UpdatePhase::Effects};

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
} // namespace NS::Obj

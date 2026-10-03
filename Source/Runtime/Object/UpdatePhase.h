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
} // namespace NS::Obj

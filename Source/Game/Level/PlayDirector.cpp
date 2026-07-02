#include "Game/Level/PlayDirector.h"

namespace NS::Game::Level
{
    PlayDirector::PlayDirector()
    {
        m_flow = AddComponent<PlayFlowComponent>();
        m_fade = AddComponent<ClearFadeComponent>();
    }

} // namespace NS::Game::Level

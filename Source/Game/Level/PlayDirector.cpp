#include "Game/Level/PlayDirector.h"

namespace NS::Game::Level
{
    PlayDirector::PlayDirector()
    {
        m_flow = AddComponent<PlayFlowComponent>();
    }

} // namespace NS::Game::Level

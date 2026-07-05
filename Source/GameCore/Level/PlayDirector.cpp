#include "GameCore/Level/PlayDirector.h"

namespace NS::GameCore::Level
{
    PlayDirector::PlayDirector()
    {
        m_flow = AddComponent<PlayFlowComponent>();
        m_fade = AddComponent<ClearFadeComponent>();
    }

} // namespace NS::GameCore::Level

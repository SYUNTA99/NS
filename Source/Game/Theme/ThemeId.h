#pragma once

/// @file ThemeId.h
/// @brief ThemeId — Grass / Cave / Snow / Lava / Sky の 5 テーマを識別する enum class

namespace NS::Game::Theme
{

    enum class ThemeId : std::uint16_t
    {
        Grass = 0,
        Cave = 1,
        Snow = 2,
        Lava = 3,
        Sky = 4,
        Count = 5,
    };

} // namespace NS::Game::Theme

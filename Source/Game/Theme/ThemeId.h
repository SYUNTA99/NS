#pragma once

/// @file ThemeId.h
/// @brief ThemeId — 5 テーマ (Grass / Cave / Snow / Lava / Sky) を識別する enum class
///
/// @details LevelData::themeId (uint16_t) との変換は ThemeRegistry::Get(uint16_t) で吸収する

#include <cstdint>

enum class ThemeId : std::uint16_t
{
    Grass = 0,
    Cave = 1,
    Snow = 2,
    Lava = 3,
    Sky = 4,
    Count = 5,
};

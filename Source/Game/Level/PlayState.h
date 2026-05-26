#pragma once

/// @file PlayState.h
/// @brief PlayState — Play mode 中だけ存在する一時データ。 `.nslvl` には書かない。
///
/// @details LevelData (永続) と物理的に別 struct にすることで「PlayMode が誤って
/// LevelData を書き換える」 経路をコンパイル時に排除する。 PlayMode は
/// `const LevelData&` と `PlayState&` を分けて受け取る。

#include "Framework/Core/Math.h"

#include <cstdint>

namespace NS::Game::Level
{

    /// Play mode の runtime state。 Tick で mutation され、 mode exit で破棄。
    /// 永続化対象は LevelData に分離してあるためここに置かれた値は CRC32 に影響しない。
    struct PlayState
    {
        NS::Core::Vector3 playerPosition{};
        NS::Core::Vector3 playerVelocity{};
        std::int32_t coinCount = 0;
        float remainingSeconds = 0.0f;
        bool paused = false;
        bool clearTriggered = false;
        bool deathTriggered = false;
    };

} // namespace NS::Game::Level

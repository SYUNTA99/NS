#pragma once

/// @file PlayState.h
/// @brief PlayState — Play mode 中だけ存在する一時データ。 `.scene` には書かない
///
/// @details 永続の LevelData と物理的に別 struct にすることで「PlayMode が誤って
/// LevelData を書き換える」 経路をコンパイル時に排除する。 PlayMode は
/// `const LevelData&` と `PlayState&` を分けて受け取る

namespace NS::Game::Level
{

    /// Play mode の runtime state。 Tick で mutation され、 mode exit で破棄
    /// 永続化対象は LevelData に分離してあるためここに置かれた値は CRC32 に影響しない
    struct PlayState
    {
        NS::Math::Vector3 playerPosition{};
        NS::Math::Vector3 playerVelocity{};
        std::int32_t coinCount = 0;
        bool paused = false;
        bool clearTriggered = false;
        bool deathTriggered = false;
        /// HazardComponent が overlap で 1 ずつ減算する 8 段階ヘルス
        std::int8_t playerHealth = 8;
    };

} // namespace NS::Game::Level

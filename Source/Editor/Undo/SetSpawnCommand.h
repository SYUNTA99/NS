#pragma once

/// @file SetSpawnCommand.h
/// @brief プレイヤー spawn の位置 + 向きを before → after で置換する Command
///
/// @details エディタで実プレイヤーをギズモ変形した結果を 1 単位で undo するために使う
/// spawn は objects に属さない単一値なので id ではなく LevelData の spawn フィールドへ直接書く
/// 値が 7 つの float だけで heap を持たないため、 実装はヘッダ内に閉じて翻訳単位を増やさない

#include "Editor/Undo/ICommand.h"
#include "Game/Level/EditTarget.h"
#include "Game/Level/LevelData.h"

#include <cstddef>

namespace NS::Editor
{
    class SetSpawnCommand final : public ICommand
    {
    public:
        /// spawn の位置 (xyz) + 向き quaternion (xyzw) の before/after スナップショット
        struct SpawnState
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float rotationX = 0.0f;
            float rotationY = 0.0f;
            float rotationZ = 0.0f;
            float rotationW = 1.0f;

            [[nodiscard]] bool operator==(const SpawnState& other) const noexcept = default;
        };

        SetSpawnCommand(const SpawnState& before, const SpawnState& after) noexcept : m_before(before), m_after(after)
        {}

        void Do(NS::Game::Level::EditTarget& target) noexcept override { Write(target, m_after); }
        void Undo(NS::Game::Level::EditTarget& target) noexcept override { Write(target, m_before); }

        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return sizeof(SetSpawnCommand); }

    private:
        static void Write(NS::Game::Level::EditTarget& target, const SpawnState& state) noexcept
        {
            target.level.spawnX = state.x;
            target.level.spawnY = state.y;
            target.level.spawnZ = state.z;
            target.level.spawnRotationX = state.rotationX;
            target.level.spawnRotationY = state.rotationY;
            target.level.spawnRotationZ = state.rotationZ;
            target.level.spawnRotationW = state.rotationW;
        }

        SpawnState m_before;
        SpawnState m_after;
    };
} // namespace NS::Editor

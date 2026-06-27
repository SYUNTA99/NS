#pragma once

/// @file BlockTypeRegistry.h
/// @brief 配置物の種別を 1 表へ集約する登録簿
///
/// @details 種別 (旧 kind 値) を引いて表示名・ パレット対象・ grid 構築レシピを得る単一窓口
/// grid 配置物の materialize と palette の種別列挙が同じ表を引き、 種別の追加は記述子 1 件で済む
/// 依存: NS::Game::Level::ObjectInstance / ComponentData (前方宣言、 実体は recipe 実装が触る)

#include <cstdint>
#include <span>
#include <vector>

namespace NS::Game::Level
{
    struct ObjectInstance;
    struct ComponentData;
} // namespace NS::Game::Level

namespace NS::Game::Blocks
{
    /// grid 配置物 1 件分の component 一覧を組む関数。 object は寸法を要する種別のために渡す
    using BlockRecipe = std::vector<NS::Game::Level::ComponentData> (*)(const NS::Game::Level::ObjectInstance&);

    /// 配置物 1 種別の記述子。 種別の追加は中央 switch でなくこの記述子 1 件の追加で済む
    struct BlockTypeDescriptor
    {
        /// 引き当てキー。 旧 kind 値と同じ
        std::uint16_t id = 0;
        /// toolbar の表示名。 ASCII 固定文字列
        const char* displayName = "?";
        /// spawn marker の種別か
        bool isSpawn = false;
        /// toolbar スロットに出す種別か
        bool paletteSlot = false;
        /// grid 構築レシピ。 spawn や視覚のみ marker は nullptr
        BlockRecipe recipe = nullptr;
    };

    /// 全種別の記述子を登録順で返す。 paletteSlot を絞ると toolbar スロット並びになる
    [[nodiscard]] std::span<const BlockTypeDescriptor> BlockTypeDescriptors() noexcept;

    /// id (旧 kind) に一致する記述子を返す。 無ければ nullptr
    [[nodiscard]] const BlockTypeDescriptor* FindBlockType(std::uint16_t id) noexcept;
} // namespace NS::Game::Blocks

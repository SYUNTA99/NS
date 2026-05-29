#pragma once

/// @file BlockRegistry.h
/// @brief 編集時に扱う block / marker の ID 体系。 ID -> 表示名 / 色 / 種別判定の lookup を提供する。
///
/// @details `LevelData::BlockEntry::blockId` に格納される値。 ID 体系をテクスチャテーブル /
/// 振る舞いテーブルに流用する。  が要求する「テーマあたり 8 variant 以上」 は
/// blockId 値を 8 枚個別に切らずに、 「block kind は 1 つ (kBlockIdSolid) / 描画時に
/// `AutoTile::LookupTextureSlice(theme, neighborMask, blockId)` で 8 slice から 1 つ選ぶ」
/// 設計で達成する。 これにより LevelData フォーマット変更ゼロで variation を実現する。
/// 今後 (04-05/06/07) で `kBlockIdSlope45 / kBlockIdPole / kBlockIdFence / kBlockIdHazard
/// / kBlockIdWater / kBlockIdDecoration` を追加していく。

#include "Framework/Core/Math.h"

#include <cstdint>

namespace NS::Game::Editor
{
    /// 通常の固形ブロック。 描画時のテクスチャは theme + neighborMask で 8 slice から決まる。
    inline constexpr std::uint16_t kBlockIdSolid = 1;

    /// 取得アイテムのコイン。
    inline constexpr std::uint16_t kBlockIdCoin = 100;

    /// クリア条件にもなり得るパワースター。
    inline constexpr std::uint16_t kBlockIdPowerStar = 101;

    /// toolbar の表示用識別子。 実体は `LevelData::spawnX/Y/Z` に書く。
    inline constexpr std::uint16_t kBlockIdSpawn = 102;

    /// 楔形 (wedge) スロープ 4 種 (-01)。 200 番台を斜面系に予約する。
    inline constexpr std::uint16_t kBlockIdSlope45 = 200;
    inline constexpr std::uint16_t kBlockIdSlope30 = 201;
    inline constexpr std::uint16_t kBlockIdSlope22 = 202;
    inline constexpr std::uint16_t kBlockIdSlope15 = 203;

    /// 掴まり系 ( / )。 210 番台を climb 系に予約する。
    inline constexpr std::uint16_t kBlockIdPole = 210;
    inline constexpr std::uint16_t kBlockIdFence = 211;

    /// 任意 blockId が 4 種 slope のいずれかかを判定する。
    [[nodiscard]] bool IsSlopeBlock(std::uint16_t blockId) noexcept;

    /// slope の blockId に対応する角度 (度数法) を返す。 slope でなければ 0。
    [[nodiscard]] float GetSlopeAngleDegrees(std::uint16_t blockId) noexcept;

    /// 掴まり pole かどうか。
    [[nodiscard]] bool IsPoleBlock(std::uint16_t blockId) noexcept;

    /// 掴まり fence かどうか。
    [[nodiscard]] bool IsFenceBlock(std::uint16_t blockId) noexcept;

    /// 各 ID に紐づく Toolbar 表示名 (ASCII 固定で ImGui label 直渡し可能)。
    [[nodiscard]] const char* GetDisplayName(std::uint16_t blockId) noexcept;

    /// 各 ID に紐づく base color (RGBA float)。 テクスチャが揃うまでの色分け用。
    [[nodiscard]] NS::Core::Color GetBaseColor(std::uint16_t blockId) noexcept;

    /// 「衝突 cube 1 個分の固形ブロック」 系か否かを返す。
    ///  時点では `kBlockIdSolid` のみが該当、 今後 slope / pole / fence 等が加わるが、
    /// それらは独自の collider component で扱うので本判定の対象外。
    [[nodiscard]] bool IsSolidBlock(std::uint16_t blockId) noexcept;

    /// プレイヤーが触ったときに衝突解決を必要とするか。
    /// 04-05/06/07 で導入予定の slope / pole / fence / hazard は true、 water / decoration は false。
    ///  時点では IsSolidBlock と一致するが、 将来差分が出るので独立した述語にしておく。
    [[nodiscard]] bool IsCollidable(std::uint16_t blockId) noexcept;
} // namespace NS::Game::Editor

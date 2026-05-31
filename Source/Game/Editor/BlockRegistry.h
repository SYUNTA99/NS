#pragma once

/// @file BlockRegistry.h
/// @brief 編集時に扱う block / marker の ID 体系。 ID -> 表示名 / 色 / 種別判定の lookup を提供する。
///
/// @details `LevelData::BlockEntry::blockId` に格納される値。 ID 体系をテクスチャテーブル /
/// 振る舞いテーブルに流用する。 「テーマあたり 8 variant 以上」 は
/// blockId 値を 8 枚個別に切らずに、 「block kind は 1 つ (kBlockIdSolid) / 描画時に
/// `AutoTile::LookupTextureSlice(theme, neighborMask, blockId)` で 8 slice から 1 つ選ぶ」
/// 設計で達成する。 これにより LevelData フォーマット変更ゼロで variation を実現する。
/// slope 4 種 (200..203) / pole (210) / hazard (220) / water (221) / decoration (222) を扱う。
/// 将来、 敵 / ギミック等を 300 番台以降で予約する。

#include "Framework/Core/Math.h"

#include <cstdint>

namespace NS::Game::Editor
{
    /// block の向きを表す `BlockEntry::rotation` の分解能。 0..3 を Y 軸 90° 刻みの 4 方向へ割り当てる。
    inline constexpr std::uint16_t kBlockRotationSteps = 4;

    /// `BlockEntry::rotation` (0..3) を Y 軸 yaw (ラジアン) に変換する。
    /// 描画と当たり判定 (BuildWedgeTriangles) が同じ向きになるよう全経路でこれを使う。
    [[nodiscard]] float BlockRotationToYaw(std::uint8_t rotation) noexcept;

    /// 通常の固形ブロック。 描画時のテクスチャは theme + neighborMask で 8 slice から決まる。
    inline constexpr std::uint16_t kBlockIdSolid = 1;

    /// 取得アイテムのコイン。
    inline constexpr std::uint16_t kBlockIdCoin = 100;

    /// クリア条件にもなり得るパワースター。
    inline constexpr std::uint16_t kBlockIdPowerStar = 101;

    /// toolbar の表示用識別子。 実体は `LevelData::spawnX/Y/Z` に書く。
    inline constexpr std::uint16_t kBlockIdSpawn = 102;

    /// 楔形 (wedge) スロープ 4 種。 200 番台を斜面系に予約する。
    inline constexpr std::uint16_t kBlockIdSlope45 = 200;
    inline constexpr std::uint16_t kBlockIdSlope30 = 201;
    inline constexpr std::uint16_t kBlockIdSlope22 = 202;
    inline constexpr std::uint16_t kBlockIdSlope15 = 203;

    /// 掴まり pole。 210 番台を掴まり系に予約する。
    inline constexpr std::uint16_t kBlockIdPole = 210;

    /// 接触ダメージ / 視覚装飾系。 220 番台を予約する。
    inline constexpr std::uint16_t kBlockIdHazard = 220;
    inline constexpr std::uint16_t kBlockIdWater = 221;
    inline constexpr std::uint16_t kBlockIdDecoration = 222;

    /// 任意 blockId が 4 種 slope のいずれかかを判定する。
    [[nodiscard]] bool IsSlopeBlock(std::uint16_t blockId) noexcept;

    /// slope の blockId に対応する角度 (度数法) を返す。 slope でなければ 0。
    [[nodiscard]] float GetSlopeAngleDegrees(std::uint16_t blockId) noexcept;

    /// slope の角度を 1 段階切り替える (45→30→22→15→45)。 slope 以外はそのまま返す。
    /// 9 スロット固定の palette で slope スロット再選択時に角度を循環させる用途。
    [[nodiscard]] std::uint16_t NextSlopeBlock(std::uint16_t blockId) noexcept;

    /// 編集中に R で 90° 回転させる対象の block か。 向きが意味を持つ slope と通常の固形 block が true。
    /// pole (Y 対称) / water / decoration は回しても見た目が変わらないので false。
    [[nodiscard]] bool IsRotatableBlock(std::uint16_t blockId) noexcept;

    /// 掴まり pole かどうか。
    [[nodiscard]] bool IsPoleBlock(std::uint16_t blockId) noexcept;

    /// 接触ダメージ hazard かどうか。
    [[nodiscard]] bool IsHazardBlock(std::uint16_t blockId) noexcept;

    /// 視覚のみの水 block かどうか。 非衝突。
    [[nodiscard]] bool IsWaterBlock(std::uint16_t blockId) noexcept;

    /// 視覚のみの装飾 block かどうか。 非衝突。
    [[nodiscard]] bool IsDecorationBlock(std::uint16_t blockId) noexcept;

    /// 各 ID に紐づく Toolbar 表示名 (ASCII 固定で ImGui label 直渡し可能)。
    [[nodiscard]] const char* GetDisplayName(std::uint16_t blockId) noexcept;

    /// 各 ID に紐づく base color (RGBA float)。 テクスチャが揃うまでの色分け用。
    [[nodiscard]] NS::Core::Color GetBaseColor(std::uint16_t blockId) noexcept;

    /// 「衝突 cube 1 個分の固形ブロック」 系か否かを返す。 現状 `kBlockIdSolid` のみが該当。
    /// slope / pole 等は独自の collider component で扱うので本判定の対象外。
    [[nodiscard]] bool IsSolidBlock(std::uint16_t blockId) noexcept;

    /// プレイヤーが触ったときに衝突解決を必要とするか。
    /// solid / slope / hazard は true、 water / decoration は false (素通し)。
    [[nodiscard]] bool IsCollidable(std::uint16_t blockId) noexcept;
} // namespace NS::Game::Editor

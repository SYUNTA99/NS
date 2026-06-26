#pragma once

/// @file CategoryPalette.h
/// @brief Mario Maker 風 8 スロット toolbar。 編集中の block 種別を選ぶ palette
///
/// @details 状態は active slot index と 8 個分の blockId 配列のみ
/// 入力ハンドリング (Gamepad LB/RB、 Keyboard 1-9) は `TickInput`、
/// ImGui 描画は `Render` で行う。 Render は Debug / Development build 時のみ
/// 実体があり、 Shipping では何もしない

#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/LevelData.h"

#include <cstddef>
#include <cstdint>

namespace NS::Platform
{
    class Input;
}
namespace NS::UI
{
    class ImGuiContext;
}

namespace NS::Editor
{
    /// 8 スロット toolbar の状態保持と入力ハンドラ
    class CategoryPalette
    {
    public:
        static constexpr std::size_t kSlotCount = 8;

        CategoryPalette() noexcept;
        ~CategoryPalette() noexcept = default;

        CategoryPalette(const CategoryPalette&) = delete;
        CategoryPalette& operator=(const CategoryPalette&) = delete;
        CategoryPalette(CategoryPalette&&) = delete;
        CategoryPalette& operator=(CategoryPalette&&) = delete;

        /// Gamepad LB/RB / Keyboard 1-8 で active slot を切替える
        /// `imgui` が `WantCaptureKeyboard` true を返す時は数字キー入力を無視する
        void TickInput(NS::Platform::Input* input, NS::UI::ImGuiContext* imgui) noexcept;

        /// ImGui で 9 スロット toolbar を描画する。 ImGui 非搭載 build では何もしない
        void Render() noexcept;

        [[nodiscard]] std::size_t ActiveSlot() const noexcept { return m_activeSlot; }
        [[nodiscard]] std::uint16_t SlotBlockId(std::size_t slot) const noexcept;
        [[nodiscard]] std::uint16_t CurrentBlockId() const noexcept { return m_currentTemplate.kind; }

        /// 配置に複製する現在のプロトタイプ。 active slot (slope は選択中の角度) のテンプレート
        [[nodiscard]] const NS::Game::Level::ObjectInstance& CurrentTemplate() const noexcept
        {
            return m_currentTemplate;
        }

        /// active slot の表示名。 slope は角度別 variant 名を返す
        [[nodiscard]] const char* CurrentTemplateName() const noexcept;

        /// 範囲外指定は無視する
        void SetActiveSlot(std::size_t slot) noexcept;

        /// active slot が slope なら角度を 1 段階循環させる (45→30→22→15→45)。 slope 以外は何もしない
        /// 固定スロットの toolbar で 4 種の slope 角度を扱うため、 スロット再選択で variant を切替える
        void CycleActiveVariant() noexcept;

    private:
        /// active slot の kind から m_currentTemplate を組み直す。 slot / variant を変えた直後に呼ぶ
        void RefreshCurrentTemplate() noexcept;

        std::size_t m_activeSlot = 0;
        // 8 スロット = 固形 / コイン / スター / spawn + 地形系 4 種 (slope / pole / hazard / water)
        // slope スロットは再選択で 45→30→22→15° を循環 (CycleActiveVariant)
        std::uint16_t m_slots[kSlotCount] = {NS::Game::Blocks::kBlockIdSolid,
                                             NS::Game::Blocks::kBlockIdCoin,
                                             NS::Game::Blocks::kBlockIdPowerStar,
                                             NS::Game::Blocks::kBlockIdSpawn,
                                             NS::Game::Blocks::kBlockIdSlope45,
                                             NS::Game::Blocks::kBlockIdPole,
                                             NS::Game::Blocks::kBlockIdHazard,
                                             NS::Game::Blocks::kBlockIdWater};

        // active slot に対応する配置プロトタイプ。 SlotBlockId は kind 索引、 こちらは複製元の実体
        NS::Game::Level::ObjectInstance m_currentTemplate{};
    };
} // namespace NS::Editor

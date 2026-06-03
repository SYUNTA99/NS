#pragma once

/// @file EditorMode.h
/// @brief 編集モード sub-system — cursor / palette / UndoStack を集約する
///
/// @details `LevelEditorScene` の value member として保有され、
/// SetActive(false) で Tick / Render が no-op になる (mode toggle 用)
/// LevelData への変更は **全て** `UndoStack::Push` 経由で発火し、
/// PlayMode 側との変更経路衝突を防ぐ。 spawn marker のみ単一値の上書きなので
/// Command を介さない直接 setter (`SetSpawnMarker`) を呼ぶ

#include "Framework/Math/Math.h"
#include "Game/Editor/CategoryPalette.h"
#include "Game/Editor/LevelFileBrowser.h"
#include "Game/Undo/UndoStack.h"

#include <cstdint>

namespace NS::Platform
{
    class Input;
}
namespace NS::UI
{
    class ImGuiContext;
}
namespace NS::Scene
{
    class CameraComponent;
    class EditorCameraComponent;
} // namespace NS::Scene
namespace NS::Game::Level
{
    struct LevelData;
}

namespace NS::Game::Editor
{
    /// 編集モード本体。 入力 / cursor / palette / UndoStack を 1 か所に集約する
    class EditorMode
    {
    public:
        /// 編集中の cursor 状態。 raycast 結果と placement 候補 cell を保持する
        struct CursorState
        {
            bool valid = false;                  ///< 何かしらヒットあり (block 面 or ground)
            NS::Math::Vector3 placementCenter{}; ///< 配置先 cell の世界座標 (cell 中心)
            NS::Math::Vector3 deleteCenter{};    ///< 削除対象 cell の世界座標
            bool placementBlocked = false;       ///< 既に block ある→赤表示
            std::int16_t hitX = 0;
            std::int16_t hitY = 0;
            std::int16_t hitZ = 0;
            std::int16_t placeX = 0;
            std::int16_t placeY = 0;
            std::int16_t placeZ = 0;
            NS::Math::Vector3 hitNormal{};
        };

        EditorMode() noexcept = default;
        ~EditorMode() noexcept = default;

        EditorMode(const EditorMode&) = delete;
        EditorMode& operator=(const EditorMode&) = delete;
        EditorMode(EditorMode&&) = delete;
        EditorMode& operator=(EditorMode&&) = delete;

        void SetLevel(NS::Game::Level::LevelData* level) noexcept { m_level = level; }
        void SetInput(NS::Platform::Input* input) noexcept { m_input = input; }
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept { m_imgui = imgui; }
        void SetCameraComponent(NS::Scene::CameraComponent* camera) noexcept { m_camera = camera; }
        void SetEditorCamera(NS::Scene::EditorCameraComponent* editorCam) noexcept { m_editorCamera = editorCam; }

        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        /// fixed step での Tick。 cursor 更新 + 入力 → Place / Delete / Rotate / Spawn / Undo / Redo を発火
        void Tick() noexcept;

        /// variable frame で cursor preview の `DebugDraw::AABB` を 1 frame 分蓄積する
        void RenderCursorPreview() noexcept;

        /// LevelData.spawnX/Y/Z の位置に常時表示する 1m wireframe (黄色)
        /// 編集モードで spawn を視覚的に把握できるようにする。 カーソル preview と独立
        void RenderSpawnMarker() noexcept;

        [[nodiscard]] const NS::Game::Undo::UndoStack& Undo() const noexcept { return m_undo; }
        [[nodiscard]] NS::Game::Undo::UndoStack& Undo() noexcept { return m_undo; }

        /// programmatic API: Tick 経路を介さずに同等の変更を発火する (テスト / 一括処理用)
        void PlaceUnderCursorProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;
        void DeleteAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;
        void RotateAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;
        void SetSpawnAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;

        /// LevelData の変更通知用 dirty flag
        /// 全 LevelData mutation で内部的に true、 LevelEditorScene::OnUpdate でチェック → rebuild
        /// 毎 frame rebuild の alloc churn (60fps × 100 block = 6000 alloc/sec) を回避する
        [[nodiscard]] bool IsLevelDirty() const noexcept { return m_levelDirty; }
        void ClearLevelDirty() noexcept { m_levelDirty = false; }

        [[nodiscard]] const CursorState& Cursor() const noexcept { return m_cursor; }
        [[nodiscard]] CategoryPalette& Palette() noexcept { return m_palette; }
        [[nodiscard]] std::uint16_t CurrentBlockId() const noexcept { return m_palette.CurrentBlockId(); }
        [[nodiscard]] std::uint8_t CurrentRotation() const noexcept { return m_currentRotation; }

        /// テスト経路で cursor 状態を直接注入する。 Tick を呼ばずに RenderCursorPreview を検証する用途
        void SetCursorForTest(const CursorState& state) noexcept { m_cursor = state; }

        /// Ctrl+S / Ctrl+O の edge を検出して file browser modal を開く。 Tick 末尾から呼ばれる
        /// ImGui がキーボードを掴んでいる時 (テキスト入力 focus 中) は無視する
        void HandleSaveLoadInput() noexcept;

        /// EditorLayer::OnRender から呼ぶ。 modal の描画 + OK 押下時の SaveLevelToFile /
        /// LoadLevelFromFile 実行 + UndoStack の clear (新 level open 時) を担う
        void RenderFileBrowser() noexcept;

        [[nodiscard]] LevelFileBrowser& FileBrowser() noexcept { return m_fileBrowser; }

    private:
        NS::Game::Level::LevelData* m_level = nullptr;
        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;
        NS::Scene::CameraComponent* m_camera = nullptr;
        NS::Scene::EditorCameraComponent* m_editorCamera = nullptr;

        bool m_active = true;
        bool m_levelDirty = true; // 初期 true。 初回 LevelEditorScene::OnUpdate で seed level の rebuild を走らせる
        std::uint8_t m_currentRotation = 0;

        CursorState m_cursor{};
        CategoryPalette m_palette{};
        NS::Game::Undo::UndoStack m_undo;
        LevelFileBrowser m_fileBrowser{};

        /// カーソル preview / 配置プレビューに使う「表示中の回転」 (Y 軸 yaw)
        /// R キーで `m_currentRotation` が即時切替わっても、 本値は Slerp で滑らかに追従し
        /// 回転方向を視覚的に把握できるようにする。 物理 / 配置データには影響しない (表示専用)
        NS::Math::Quaternion m_displayedYawQuat{NS::Math::Quaternion::Identity};

        void UpdateCursorFromInput() noexcept;
        void HandlePlaceDeleteInput() noexcept;
        void HandleRotationInput() noexcept;
        void HandleUndoRedoInput() noexcept;
    };
} // namespace NS::Game::Editor

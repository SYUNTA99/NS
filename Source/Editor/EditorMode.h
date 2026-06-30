#pragma once

/// @file EditorMode.h
/// @brief 編集モード sub-system — cursor / palette / UndoStack を集約する
///
/// @details `LevelEditorController` の value member として保有され、
/// mode toggle 用に SetActive(false) で Tick / Render が何もしない
/// LevelData への変更は **全て** `UndoStack::Push` 経由で発火し、
/// PlayMode 側との変更経路衝突を防ぐ

#include "Editor/CategoryPalette.h"
#include "Editor/LevelFileBrowser.h"
#include "Editor/Undo/UndoStack.h"
#include "Framework/Math/Math.h"

#include <cstdint>
#include <string>
#include <string_view>

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
} // namespace NS::Scene
namespace NS::Game::Level
{
    struct LevelData;
}

namespace NS::Editor
{
    /// 編集モード本体。 入力 / cursor / palette / UndoStack を 1 か所に集約する
    class EditorMode
    {
    public:
        /// 編集中の cursor 状態。 raycast 結果と placement 候補 cell を保持する
        struct CursorState
        {
            /// 何かしらヒットあり。 block 面か ground
            bool valid = false;
            /// 配置先 cell の世界座標で cell 中心
            NS::Math::Vector3 placementCenter{};
            /// 削除対象 cell の世界座標
            NS::Math::Vector3 deleteCenter{};
            /// 既に block があり赤表示
            bool placementBlocked = false;
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
        /// 編集セッション id ストアを注入する。 EditTarget の構築に使い、 level と同じ scene が所有する
        void SetEditIds(std::vector<std::uint32_t>* ids, std::uint32_t* nextId) noexcept
        {
            m_objectIds = ids;
            m_nextObjectId = nextId;
        }
        void SetInput(NS::Platform::Input* input) noexcept { m_input = input; }
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept { m_imgui = imgui; }
        void SetCameraComponent(NS::Scene::CameraComponent* camera) noexcept { m_camera = camera; }

        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        /// Object ツールモード中など、 設置 / 削除 / 回転 / undo の grid 編集入力を一時的に無視させる
        void SetInputSuppressed(bool suppressed) noexcept { m_inputSuppressed = suppressed; }

        /// fixed step での Tick。 cursor 更新 + 入力 → Place / Delete / Rotate / Spawn / Undo / Redo を発火
        void Tick() noexcept;

        /// variable frame で cursor preview の `DebugDraw::AABB` を 1 frame 分蓄積する
        void RenderCursorPreview() noexcept;

        [[nodiscard]] const NS::Editor::UndoStack& Undo() const noexcept { return m_undo; }
        [[nodiscard]] NS::Editor::UndoStack& Undo() noexcept { return m_undo; }

        /// programmatic API: Tick 経路を介さずに同等の変更を発火する。 テスト / 一括処理用
        void PlaceUnderCursorProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;
        void DeleteAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;
        void RotateAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept;

        /// mutation で true になる dirty flag。 毎フレーム rebuild を避けるため検出時のみ rebuild を走らせる
        [[nodiscard]] bool IsLevelDirty() const noexcept { return m_levelDirty; }
        void ClearLevelDirty() noexcept { m_levelDirty = false; }

        [[nodiscard]] CategoryPalette& Palette() noexcept { return m_palette; }

        /// テスト経路で cursor 状態を直接注入する。 Tick を呼ばずに RenderCursorPreview を検証する用途
        void SetCursorForTest(const CursorState& state) noexcept { m_cursor = state; }

        /// 保存 / 読込のキー入力を捌く。 Tick 末尾から呼ばれ、 テキスト入力 focus 中は無視する
        /// Ctrl+S は現在レベルへ上書き保存、 Ctrl+Shift+S と未保存時は名前付け保存、 Ctrl+O は読込
        void HandleSaveLoadInput() noexcept;

        /// modal 描画 + OK 押下時の Save/Load 実行 + 新規 open 時の UndoStack clear を担う
        void RenderFileBrowser() noexcept;

        /// 終了確認の保存に使う。 現在レベル名へ、 無ければ起動レベル new_level へ書き出す。 成功で true
        [[nodiscard]] bool SaveForQuit() noexcept;

    private:
        NS::Game::Level::LevelData* m_level = nullptr;
        std::vector<std::uint32_t>* m_objectIds = nullptr; // m_level.objects と 1:1 の編集 id。scene が所有する
        std::uint32_t* m_nextObjectId = nullptr;
        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;
        NS::Scene::CameraComponent* m_camera = nullptr;

        bool m_active = true;
        bool m_inputSuppressed = false; // Object ツールモード中は grid 編集入力を無視する
        bool m_levelDirty = true;       // 初期 true。 編集側の初回 rebuild 判定に使う
        std::uint8_t m_currentRotation = 0;

        CursorState m_cursor{};
        CategoryPalette m_palette{};
        NS::Editor::UndoStack m_undo;
        LevelFileBrowser m_fileBrowser{};

        // 現在開いているレベル名。 空なら未保存の新規で、 上書き保存は名前付け保存にフォールバックする
        std::string m_currentLevelName;

        // 上書き保存などモーダル外操作の結果を数秒だけ画面上部に出す通知
        std::string m_statusMessage;
        bool m_statusError = false;
        float m_statusTimer = 0.0f;

        /// Slerp で m_currentRotation に追従する表示専用 yaw。 物理・配置データには影響しない
        NS::Math::Quaternion m_displayedYawQuat{NS::Math::Quaternion::Identity};

        void UpdateCursorFromInput() noexcept;
        void HandlePlaceDeleteInput() noexcept;
        void HandleRotationInput() noexcept;
        void HandleUndoRedoInput() noexcept;

        /// name へレベルを書き出し、 成功時に m_currentLevelName を更新する。 保存 I/O の単一窓口
        [[nodiscard]] bool SaveLevelToName(std::string_view name) noexcept;

        /// 現在レベルへ上書き保存し、 結果を通知へ流す。 Ctrl+S 経路
        void OverwriteCurrentLevel() noexcept;

        /// m_level + id ストアから EditTarget view を組む。 全ポインタ非 null の前提で呼ぶ
        [[nodiscard]] NS::Game::Level::EditTarget Target() noexcept;
    };
} // namespace NS::Editor

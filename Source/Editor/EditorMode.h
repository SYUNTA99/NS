#pragma once

#include "Editor/CategoryPalette.h"
#include "Editor/GridMath.h"
#include "Editor/LevelFileBrowser.h"
#include "Editor/Undo/UndoStack.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <functional>

namespace NS
{
    namespace OS
    {
        class Input;
    }
} // namespace NS
namespace NS
{
    namespace UI
    {
        class ImGuiContext;
    }
} // namespace NS
namespace NS
{
    class SceneCamera;
} // namespace NS
namespace NS
{
    namespace Gfx
    {
        class DebugShapes;
    }
} // namespace NS
namespace NS
{
    namespace Editor
    {
        class IObjectSnapshotApplier;

        //! グリッド配置・保存・Undo の編集機能。非アクティブ中は編集入力と描画を止める
        class EditorMode : public NonCopyable
        {
        public:
            //! @brief セルの整数座標。実体の照会に使う
            struct CellCoord
            {
                int16 x = 0;
                int16 y = 0;
                int16 z = 0;
            };

            //! @brief 編集中のカーソル状態。レイキャスト結果や配置候補のセル情報を保持する
            struct CursorState
            {
                bool valid = false;            //!< ブロック面か地面に当たったか
                Vector3 placementCenter{}; //!< 配置先セルの中心ワールド座標
                bool placementBlocked = false; //!< 配置予定地に種類を問わず配置物が居るか
                CellCoord hit{};               //!< ヒットしたセル
                CellCoord place{};             //!< 配置先セル
            };

            //! @brief 保存時に実体からシーンの JSON 文書を作る捕捉関数を差す。未設定なら保存できない
            void SetCaptureLevelFn(std::function<nlohmann::json()> fn) noexcept { m_captureLevel = std::move(fn); }

            //! @brief セル編集・取り消しを実体へ適用する処理を設定する。未設定ならセル編集は何もしない
            void SetApplier(IObjectSnapshotApplier* applier) noexcept { m_applier = applier; }

            //! @brief 読込済みシーンデータを実体側へ取り込む関数を差す。読込の完了時に呼ぶ
            void SetLoadLevelFn(std::function<void(nlohmann::json&&)> fn) noexcept { m_loadLevel = std::move(fn); }

            //! @brief セルにあるセルブラシ配置物の永続識別番号を取得する処理を設定する。不在は k_NoObjectId
            void SetFindCellObjectFn(std::function<uint32(int16, int16, int16)> fn) noexcept
            {
                m_findCellObject = std::move(fn);
            }

            //! @brief cell に種類を問わず配置物が居るかを引く関数を差す
            void SetCellOccupiedFn(std::function<bool(int16, int16, int16)> fn) noexcept
            {
                m_cellOccupied = std::move(fn);
            }

            //! @brief 全セルブラシ配置物の座標を実体から取得する処理を設定する。カーソルの光線判定で使う
            void SetCollectCellsFn(std::function<std::vector<CellCoord>()> fn) noexcept
            {
                m_collectCells = std::move(fn);
            }

            //! @brief 新規配置物へ永続 id を 1 個振る関数を差す
            void SetAllocateIdFn(std::function<uint32()> fn) noexcept { m_allocateId = std::move(fn); }
            void SetInput(OS::Input* input) noexcept { m_input = input; }
            void SetImGui(UI::ImGuiContext* imgui) noexcept { m_imgui = imgui; }
            void SetSceneCamera(SceneCamera* camera) noexcept { m_camera = camera; }

            //! 休止の間はプレイ中として扱い、保存も受け付けない
            void SetActive(bool active) noexcept { m_active = active; }
            [[nodiscard]] bool IsActive() const noexcept { return m_active; }

            //! グリッドに対する編集入力を一時的に無効化する
            void SetInputSuppressed(bool suppressed) noexcept { m_inputSuppressed = suppressed; }

            //! 編集入力が基準にする表示矩形を渡す。カーソルのレイはこの矩形基準で飛ぶ
            void SetViewRect(const ViewRect& rect) noexcept { m_viewRect = rect; }

            //! マウスが表示矩形のパネル上に居るかを渡す。偽の間は配置カーソルを立てない
            void SetViewHovered(bool hovered) noexcept { m_viewHovered = hovered; }

            //! ギズモドラッグ中だけ undo/redo を止める。旧 Transform への書込を防ぐ
            void SetUndoRedoSuppressed(bool suppressed) noexcept { m_undoRedoSuppressed = suppressed; }

            void Tick() noexcept;

            //! @brief カーソル位置に置いた時のプレビュー枠を、Scene タブの図形として積む
            //! @param[in,out] shapes 積む先。そのビューを描いたら捨てられる
            void DrawCursorShapes(Gfx::DebugShapes& shapes) const noexcept;

            //! @brief カーソル位置のセルの枠とスロープの向きを ImGui の前面へ描く
            //! @details ImGui のフレームの中で呼ぶ。立体の枠は DrawCursorShapes が積む
            void RenderCursorPreview() noexcept;

            [[nodiscard]] const Editor::UndoStack& Undo() const noexcept { return m_undo; }
            [[nodiscard]] Editor::UndoStack& Undo() noexcept { return m_undo; }

            //! @brief テストや自動処理向けに、入力を介さずに指定座標へ直接編集操作を実行する
            void PlaceUnderCursorProgrammatic(int16 x, int16 y, int16 z) noexcept;
            void DeleteAtProgrammatic(int16 x, int16 y, int16 z) noexcept;
            void RotateAtProgrammatic(int16 x, int16 y, int16 z) noexcept;

            //! @brief 最後の保存 / 読込のあとに編集が入ったか
            [[nodiscard]] bool HasUnsavedChanges() const noexcept { return m_undo.Version() != m_savedUndoVersion; }

            //! @brief 今開いているレベル名。まだ名前が決まっていなければ空
            [[nodiscard]] const std::string& CurrentLevelName() const noexcept { return m_currentLevelName; }

            [[nodiscard]] bool IsLevelDirty() const noexcept { return m_levelDirty; }
            void ClearLevelDirty() noexcept { m_levelDirty = false; }

            [[nodiscard]] CategoryPalette& Palette() noexcept { return m_palette; }

            void HandleSaveLoadInput() noexcept;

            //! 保存 / 読込のモーダルを描き、確定した操作をその場で実行する
            void RenderFileBrowser() noexcept;

            //! @brief 終了する前に保存する
            [[nodiscard]] bool SaveForQuit() noexcept;

            //! @brief 起動レベルが実在するのに読めなかったことを印す。終了保存が既存ファイルを上書きしないようにする
            void MarkBootLevelLoadFailed() noexcept { m_bootLevelLoadFailed = true; }

            //! 1 手戻す。戻せたら true。メニューとキーが同じ入口で呼ぶ
            bool PerformUndo() noexcept;
            //! 1 手やり直す。やり直せたら true
            bool PerformRedo() noexcept;
            [[nodiscard]] bool CanUndo() const noexcept { return m_undo.UndoSize() > 0; }
            [[nodiscard]] bool CanRedo() const noexcept { return m_undo.RedoSize() > 0; }

            //! 現在名で上書き保存。名前が無ければ名前を付けて保存を開く
            void RequestSave() noexcept;
            //! 名前を付けて保存のモーダルを開く
            void OpenSaveModal() noexcept { m_fileBrowser.OpenSaveModal(m_currentLevelName); }
            void OpenLoadModal() noexcept { m_fileBrowser.OpenLoadModal(); }

        private:
            std::function<nlohmann::json()> m_captureLevel;
            IObjectSnapshotApplier* m_applier = nullptr;
            std::function<void(nlohmann::json&&)> m_loadLevel;
            std::function<uint32(int16, int16, int16)> m_findCellObject;
            std::function<bool(int16, int16, int16)> m_cellOccupied;
            std::function<std::vector<CellCoord>()> m_collectCells;
            std::function<uint32()> m_allocateId;
            OS::Input* m_input = nullptr;
            UI::ImGuiContext* m_imgui = nullptr;
            SceneCamera* m_camera = nullptr;

            bool m_active = true;
            bool m_inputSuppressed = false;
            bool m_undoRedoSuppressed = false; // ギズモドラッグ中だけ立てて undo/redo を止める
            bool m_levelDirty = true;
            ViewRect m_viewRect{};           // 編集入力が基準にする表示矩形。幅 0 の間は入力を受けない
            bool m_viewHovered = true;       // マウスがパネル上に居るか。全画面直描き時は常に真
            uint8 m_currentRotation = 0; // 90度刻みの回転状態 (0〜3)

            CursorState m_cursor{};
            CategoryPalette m_palette{};
            Editor::UndoStack m_undo;
            LevelFileBrowser m_fileBrowser{};

            std::string m_currentLevelName;
            bool m_bootLevelLoadFailed = false; // 起動レベルが実在かつ読込失敗した
            uint64 m_savedUndoVersion = 0;  // 最後に保存 / 読込した時点の履歴の版

            std::string m_statusMessage;
            bool m_statusError = false;
            float m_statusTimer = 0.0f;

            // プレビュー表示用の滑らかな回転状態
            Quaternion m_displayedYawQuat{Quaternion::Identity};

            void UpdateCursorFromInput() noexcept;
            void HandlePlaceDeleteInput() noexcept;
            void HandleRotationInput() noexcept;
            void HandleUndoRedoInput() noexcept;

            //! セルにセルブラシ配置物があるか。実体の照会が未設定なら常に不在
            [[nodiscard]] bool HasObjectAtCell(int16 x, int16 y, int16 z) const noexcept;
            //! cell に種類を問わず配置物が居るか。置けるかの判定だけが読む
            [[nodiscard]] bool IsCellOccupied(int16 x, int16 y, int16 z) const noexcept;

            [[nodiscard]] bool SaveLevelToName(std::string_view name) noexcept;
            void OverwriteCurrentLevel() noexcept;
        };
    } // namespace Editor
} // namespace NS

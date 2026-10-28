#pragma once

#include "Editor/CategoryPalette.h"
#include "Editor/GridMath.h"
#include "Editor/LevelFileBrowser.h"
#include "Editor/Undo/UndoStack.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"

#include <functional>
#include <vector>

namespace NS
{
    namespace OS
    {
        class Input;
        class InputPresses;
    } // namespace OS
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
        class IActorSnapshotApplier;

        //! CreateLevel の答え。作れなかった時は理由を返し、作成の窓が理由ごとの文を出す
        enum class CreateLevelResult : uint8
        {
            Created,
            BadPath,       // 名前か保存先のフォルダに使えない文字がある
            AlreadyExists, // 同じ名前のファイルがある。上書きしない
            NotEditable,   // 編集モードでない
            WriteFailed,   // ファイルへ書けなかった
        };

        //! グリッド配置・保存・Undo の編集機能。非アクティブ中は編集入力と描画を止める
        class EditorMode : public NonCopyable
        {
        public:
            //! セルの整数座標。実体の照会に使う
            struct CellCoord
            {
                int16 x = 0;
                int16 y = 0;
                int16 z = 0;
            };

            //! 編集中のカーソル状態。レイキャスト結果や配置候補のセル情報を保持する
            struct CursorState
            {
                bool valid = false;            // ブロック面か地面に当たったか
                Vector3 placementCenter{}; // 配置先セルの中心ワールド座標
                bool placementBlocked = false; // 配置予定地に種類を問わずアクターが居るか
    CellCoord hit{};               // ヒットしたセル
                CellCoord place{};             // 配置先セル
            };

            //! 保存時に実体からシーンの JSON 文書を作る捕捉関数を差す。未設定なら保存できない
            void SetCaptureLevelFn(std::function<nlohmann::json()> fn) noexcept { m_captureLevel = std::move(fn); }

            //! セル編集・取り消しを実体へ適用する処理を設定する。未設定ならセル編集は何もしない
            void SetApplier(IActorSnapshotApplier* applier) noexcept { m_applier = applier; }

            //! 読込済みシーンデータを実体側へ取り込む関数を差す。読込の完了時に呼ぶ
            void SetLoadLevelFn(std::function<void(nlohmann::json&&)> fn) noexcept { m_loadLevel = std::move(fn); }

            //! セルにあるセルブラシのアクターの永続識別番号を取得する処理を設定する。不在は k_NoActorId
            void SetFindCellActorFn(std::function<uint32(int16, int16, int16)> fn) noexcept
            {
                m_findCellActor = std::move(fn);
            }

            //! cell に種類を問わずアクターが居るかを引く関数を差す
            void SetCellOccupiedFn(std::function<bool(int16, int16, int16)> fn) noexcept
            {
                m_cellOccupied = std::move(fn);
            }

            //! 全セルブラシのアクターの座標を実体から取得する処理を設定する。カーソルの光線判定で使う
            void SetCollectCellsFn(std::function<std::vector<CellCoord>()> fn) noexcept
            {
                m_collectCells = std::move(fn);
            }

            //! 新規アクターへ永続 id を 1 個振る関数を差す
            void SetAllocateIdFn(std::function<uint32()> fn) noexcept { m_allocateId = std::move(fn); }
            void SetInput(OS::Input* input) noexcept { m_input = input; }
            void SetSceneCamera(SceneCamera* camera) noexcept { m_camera = camera; }

            //! シーンを変えてよいかを答える関数を差す。LevelEditorController::IsSceneEditable を差す
            //! 偽の間は入力・カーソル・作る・筆・取り消し・開く・保存を全部断る。未設定なら変えてよい
            void SetSceneEditableFn(std::function<bool()> fn) noexcept { m_sceneEditable = std::move(fn); }

            //! グリッドに対する編集入力を一時的に無効化する
            void SetInputSuppressed(bool suppressed) noexcept { m_inputSuppressed = suppressed; }

            //! 編集入力が基準にする表示矩形を渡す。カーソルのレイはこの矩形基準で飛ぶ
            void SetViewRect(const ViewRect& rect) noexcept { m_viewRect = rect; }

            //! マウスが表示矩形のパネル上に居るかを渡す。偽の間は配置カーソルを立てない
            void SetViewHovered(bool hovered) noexcept { m_viewHovered = hovered; }

            //! ギズモドラッグ中だけ undo/redo を止める。旧 Transform への書込を防ぐ
            void SetUndoRedoSuppressed(bool suppressed) noexcept { m_undoRedoSuppressed = suppressed; }

            //! 編集の入力を 1 固定ステップ分進める
            //! @param[in] editShortcutsBlocked ResolveInputOwnership の答え。
            //! 真の間は R・Ctrl+Z・Ctrl+S・数字のキーを受けない
            //! @param[in] presses 持ち主がこの固定ステップに読んだ押し
            void Tick(bool editShortcutsBlocked, const OS::InputPresses& presses) noexcept;

            //! カーソル位置に置いた時のプレビュー枠を、Scene タブの図形として積む
            //! @param[in,out] shapes 積む先。そのビューを描いたら捨てられる
            void DrawCursorShapes(Gfx::DebugShapes& shapes) const noexcept;

            //! カーソル位置のセルの枠とスロープの向きを ImGui の前面へ描く
            //! ImGui のフレームの中で呼ぶ。立体の枠は DrawCursorShapes が積む
            void RenderCursorPreview() noexcept;

            [[nodiscard]] const Editor::UndoStack& Undo() const noexcept { return m_undo; }
            [[nodiscard]] Editor::UndoStack& Undo() noexcept { return m_undo; }

            //! 新しいアクターを 1 回の履歴で作る。作る道 (置く・複製・筆で空いた升へ置く) は全部ここを通す
            //! 各 JSON に新しい永続 id を振り、写した元の id を指す参照を、一緒に作る物の新しい id へ付け替える。
            //! 作った物を選ぶかは呼び手が決める
            //! @return 作った物の id を渡した順に返す。休止の間 (プレイ中) と、適用処理か採番が無い時は作らずに空を返す
            [[nodiscard]] std::vector<uint32> CreateActors(std::vector<nlohmann::json> objects);

            //! テストや自動処理向けに、入力を介さずに指定座標へ直接編集操作を実行する
            void PlaceUnderCursorProgrammatic(int16 x, int16 y, int16 z) noexcept;
            void DeleteAtProgrammatic(int16 x, int16 y, int16 z) noexcept;
            void RotateAtProgrammatic(int16 x, int16 y, int16 z) noexcept;

            //! 最後の保存 / 読込のあとに編集が入ったか
            [[nodiscard]] bool HasUnsavedChanges() const noexcept { return m_undo.Version() != m_savedUndoVersion; }

            //! 今開いているレベル名。まだ名前が決まっていなければ空
            [[nodiscard]] const std::string& CurrentLevelName() const noexcept { return m_currentLevelName; }

            [[nodiscard]] bool IsLevelDirty() const noexcept { return m_levelDirty; }
            void ClearLevelDirty() noexcept { m_levelDirty = false; }

            [[nodiscard]] CategoryPalette& Palette() noexcept { return m_palette; }

            void HandleSaveLoadInput(const OS::InputPresses& presses) noexcept;

            //! 保存 / 読込のモーダルを描き、確定した操作をその場で実行する
            void RenderFileBrowser() noexcept;

            //! 終了する前に保存する。書き先は QuitSaveLevelName
            [[nodiscard]] bool SaveForQuit() noexcept;

            //! 終了の保存の書き先のレベルの名前。今のレベルの名前、起動レベルが読めなかった時は退避の名前、
            //! どちらも無ければ起動シーンの名前
            [[nodiscard]] std::string QuitSaveLevelName() const noexcept;

            //! レベルを読み、プレイヤーと落下死の範囲が無ければ補ってから実体へ取り込む
            //! 読めたら名前を控え、履歴を空にして未保存でない状態にする。起動と「開く」の両方がここを通る
            //! @return 名前が使えない・取り込み関数が無い・読めない時は false で、今のレベルと名前を保つ。
            //! 名前の無いまま実在するファイルを読めなかった時は、終了の保存の書き先を退避の名前にする
            bool OpenLevel(std::string_view name) noexcept;
            //! エディタ固有の作業状態の保存先。空なら記録と復元を行わない
            void SetSessionPath(std::string path) noexcept { m_sessionPath = std::move(path); }
            //! 前回開いたシーンを復元し、記録が無いか開けなければ fallback を開く
            [[nodiscard]] bool OpenStartupLevel(std::string_view fallback) noexcept;
            // 現在の編集内容と履歴を保ち、既存ファイルを上書きしない
            [[nodiscard]] CreateLevelResult CreateLevel(std::string_view name) noexcept;

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

        private:
            std::function<nlohmann::json()> m_captureLevel;
            IActorSnapshotApplier* m_applier = nullptr;
            std::function<void(nlohmann::json&&)> m_loadLevel;
            std::function<uint32(int16, int16, int16)> m_findCellActor;
            std::function<bool(int16, int16, int16)> m_cellOccupied;
            std::function<std::vector<CellCoord>()> m_collectCells;
            std::function<uint32()> m_allocateId;
            OS::Input* m_input = nullptr;
            SceneCamera* m_camera = nullptr;

            std::function<bool()> m_sceneEditable;
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
            std::string m_sessionPath;
            bool m_bootLevelLoadFailed = false; // 起動レベルが実在かつ読込失敗した
            uint64 m_savedUndoVersion = 0;  // 最後に保存 / 読込した時点の履歴の版

            std::string m_statusMessage;
            bool m_statusError = false;
            float m_statusTimer = 0.0f;

            // プレビュー表示用の滑らかな回転状態
            Quaternion m_displayedYawQuat{Quaternion::Identity};

            void UpdateCursorFromInput() noexcept;
            void HandlePlaceDeleteInput(const OS::InputPresses& presses) noexcept;
            void HandleRotationInput(const OS::InputPresses& presses) noexcept;
            void HandleUndoRedoInput(const OS::InputPresses& presses) noexcept;

            //! 差した関数の答え。シーンを変える関数は全部、頭でこれを見る
            [[nodiscard]] bool IsSceneEditable() const noexcept;

            //! セルにセルブラシのアクターがあるか。実体の照会が未設定なら常に不在
            [[nodiscard]] bool HasActorAtCell(int16 x, int16 y, int16 z) const noexcept;
            //! cell に種類を問わずアクターが居るか。置けるかの判定だけが読む
            [[nodiscard]] bool IsCellOccupied(int16 x, int16 y, int16 z) const noexcept;

            [[nodiscard]] bool SaveLevelToName(std::string_view name) noexcept;
            void RememberCurrentLevel() const noexcept;
            void OverwriteCurrentLevel() noexcept;
        };
    } // namespace Editor
} // namespace NS

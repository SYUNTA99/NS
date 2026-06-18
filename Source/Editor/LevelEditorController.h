#pragma once

/// @file LevelEditorController.h
/// @brief LevelPlayScene を編集する開発用コントローラ (Debug / Development 限定)
///
/// @details scene 自身は「編集されている」ことを知らない。 本クラスが LevelPlayScene の friend として
/// 内部 (runtime オブジェクト群 / camera brain / play 状態) を操作し、 EditorMode (cursor / palette / undo) ・
/// ギズモ変形・free-fly カメラ・編集 ↔ プレイのモード切替を実現する。 EditorLayer が所有し、
/// Setup / Tick / Render / Teardown を駆動する。 出荷 build には本クラスも EditorLayer も含めない

#include "Editor/EditorMode.h"
#include "Editor/GizmoEditor.h"
#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Math/Math.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace NS::Scene
{
    class GameObject;
    class Transform;
    class PlacedVirtualCamera;
} // namespace NS::Scene

namespace NS::UI
{
    class ImGuiContext;
}

class EditorCameraRig;
class LevelPlayScene;

/// LevelPlayScene を編集する開発用コントローラ。 EditorLayer が 1 個所有する
class LevelEditorController
{
public:
    explicit LevelEditorController(LevelPlayScene* scene) noexcept;
    ~LevelEditorController();

    LevelEditorController(const LevelEditorController&) = delete;
    LevelEditorController& operator=(const LevelEditorController&) = delete;
    LevelEditorController(LevelEditorController&&) = delete;
    LevelEditorController& operator=(LevelEditorController&&) = delete;

    /// scene の OnStart 完了後に呼ぶ。 free-fly カメラ / EditorMode / ギズモを立ち上げ編集モードへ入る
    /// imgui は EditorLayer 所有の context。EditorMode / ギズモの入力ゲートへ橋渡しする (非所有)
    void Setup(NS::UI::ImGuiContext* imgui);
    /// fixed step 更新。 編集中は free-fly カメラ / ギズモ / EditorMode を回す。 プレイ中はクリア監視のみ
    void Tick();
    /// render フレームの上乗せ描画 (ギズモ / palette / 編集ビジュアル) と debug provenance 退避
    void Render();
    /// scene 破棄の前に呼ぶ。 ギズモ選択解除と free-fly カメラの後始末
    void Teardown();

    /// 編集 ↔ プレイのモード状態。 単一 enum で同フレーム即切替する
    enum class Mode : std::uint8_t
    {
        Edit,
        Play
    };

    [[nodiscard]] Mode CurrentMode() const noexcept { return m_mode; }
    /// Edit → Play 遷移。 scene をプレイ開始させ free-fly カメラと編集入力を休止する
    void EnterPlay() noexcept;
    /// Play → Edit 遷移。 scene のプレイを止め free-fly カメラと編集入力を再開する
    void EnterEdit() noexcept;

    [[nodiscard]] NS::Editor::EditorMode& Editor() noexcept { return m_editor; }
    [[nodiscard]] LevelPlayScene& Scene() noexcept { return *m_scene; }
    /// panel 利便のための pass-through。 編集対象の LevelData
    [[nodiscard]] NS::Game::Level::LevelData& Level() noexcept;
    /// panel 利便のための pass-through。 一時的な PlayState
    [[nodiscard]] NS::Game::Level::PlayState& Play() noexcept;

    /// Object (ギズモ変形) ツールが有効か。 false は Build (グリッド設置)
    [[nodiscard]] bool ObjectToolActive() const noexcept { return m_editorToolMode == EditorToolMode::Object; }
    /// 編集ツールを Build / Object 切替える。 Build へ戻す時はギズモ選択を解除する
    void SetObjectToolActive(bool active) noexcept;

    /// 現在選択中の配置物の objects 添字。 未選択 / 範囲外は kNoObjectIndex
    [[nodiscard]] std::size_t SelectedObjectIndex() const noexcept { return m_selectedObjectIndex; }
    /// Hierarchy から添字で配置物を選択する。 Object ツールへ切替え、 free / grid solid はギズモ選択も貼る
    void SelectObjectByIndex(std::size_t index) noexcept;

    /// Inspector が編集 / 表示できる選択を持つか
    [[nodiscard]] bool HasInspectableSelection() const noexcept;
    /// 選択中の配置物が gridAligned か。 未選択は false
    [[nodiscard]] bool SelectedIsGridAligned() const noexcept;
    /// Inspector 表示用に選択中 ObjectInstance のコピーを返す。 未選択は既定値
    [[nodiscard]] NS::Game::Level::ObjectInstance SelectedObjectSnapshot() const noexcept;
    /// 選択中の自由オブジェクトの位置を設定する (gridAligned / 非選択は no-op)
    void SetSelectedFreePosition(NS::Math::Vector3 position) noexcept;
    /// 選択中の自由オブジェクトのスケールを設定する。 最小正値に clamp する (gridAligned / 非選択は no-op)
    void SetSelectedFreeScale(NS::Math::Vector3 scale) noexcept;
    /// 選択中の grid solid ブロックを自由オブジェクトへ昇格する (grid solid 以外は no-op)
    void PromoteSelectedToFree() noexcept;

    /// 現在選択中の area camera の cameraVolumes 添字。 未選択 / 範囲外は kNoObjectIndex
    [[nodiscard]] std::size_t SelectedCameraIndex() const noexcept { return m_selectedCameraIndex; }
    /// Hierarchy から添字で area camera を選択する。 オブジェクト / ギズモ選択は解除する
    void SelectCameraByIndex(std::size_t index) noexcept;
    /// Inspector が編集できる area camera の選択を持つか
    [[nodiscard]] bool HasCameraSelection() const noexcept;
    /// 選択中 area camera の runtime PlacedVirtualCamera。 未選択 / 範囲外は nullptr。 Inspector の反射編集対象
    [[nodiscard]] NS::Scene::PlacedVirtualCamera* SelectedAreaCamera() noexcept;
    /// 反射編集された PlacedVirtualCamera の値を選択中 CameraVolume へ書き戻す (保存に乗せる、 非選択は no-op)
    /// トリガ半径は最小正値に clamp し component 側へも反映する
    void SyncSelectedCameraVolumeFromComponent() noexcept;
    /// 編集視点の中心あたりに新しい area camera を追加して選択する
    void AddCameraVolume() noexcept;
    /// 選択中の area camera を削除する (非選択は no-op)
    void DeleteSelectedCamera() noexcept;

    /// Object モードかつギズモで何か選択中なら true (material 適用先がある状態)
    [[nodiscard]] bool HasGizmoSelection() const noexcept
    {
        return ObjectToolActive() && m_gizmo.Selected() != nullptr;
    }
    /// ギズモ選択中の自由オブジェクトに matPath の .mat を適用する。 適用できたら true
    bool ApplyMaterialToSelected(const std::filesystem::path& matPath);

    /// 最後に scene が解決した scene 段設定。 RenderSettings パネルの表示元
    [[nodiscard]] const NS::Graphics::RenderSettings& DebugResolvedSettings() const noexcept
    {
        return m_debugResolvedSettings;
    }
    /// 最後に構築した scene override。 出所逆算 (default / scene) の入力に使う
    [[nodiscard]] const NS::Graphics::RenderSettingsOverride& DebugSceneOverride() const noexcept
    {
        return m_debugSceneOverride;
    }
    /// 代表 object override (Player の MeshRenderer)。 出所逆算 (object) の入力
    [[nodiscard]] const NS::Graphics::RenderSettingsOverride& DebugPlayerObjectOverride() const noexcept
    {
        return m_debugPlayerObjectOverride;
    }

private:
    /// edit モードの fixed step: free-fly カメラ / ギズモ / EditorMode の入力処理と dirty rebuild
    void TickEdit();

    /// edit 中、 area camera のトリガ AABB とカメラ位置 → 注視点を DebugDraw で可視化する
    void RenderAreaCameraGizmos() noexcept;

    /// ギズモの選択候補 (自由オブジェクト + grid solid ブロック) を連結し直して注入する
    void RefreshGizmoSelectables();

    /// grid solid ブロックの ObjectInstance から gridAligned ビットを落として自由オブジェクト化する
    void PromoteGridBlockToFree(std::size_t blockIndex);

    /// ギズモで変形した自由オブジェクトの Transform を対応する ObjectInstance へ書き戻す
    void SyncFreeObjectTransforms();

    /// ビューポートでギズモ選択が変わった時だけ m_selectedObjectIndex を追従させる
    void ResolveSelectedIndexFromGizmo() noexcept;

    LevelPlayScene* m_scene = nullptr;

    // EditorLayer 所有の ImGui context (非所有)。EditorMode / ギズモへ渡し、 keyboard キャプチャ判定にも使う
    NS::UI::ImGuiContext* m_imgui = nullptr;

    std::unique_ptr<EditorCameraRig> m_editorCameraRig;

    NS::Editor::EditorMode m_editor{};
    Mode m_mode = Mode::Edit;

    /// 編集中の入力所有。 Build=grid 設置、 Object=ギズモ変形。 同時に 1 つだけが LMB/R/Ctrl+Z を消費
    enum class EditorToolMode : std::uint8_t
    {
        Build,
        Object
    };
    EditorToolMode m_editorToolMode = EditorToolMode::Build;

    NS::Editor::GizmoEditor m_gizmo{};

    // ギズモへ渡す選択候補の安定ストレージ。 自由オブジェクトと grid solid ブロックを連結した span の実体
    std::vector<NS::Scene::GameObject*> m_selectablePtrs;
    std::vector<NS::Math::Vector3> m_selectableHalfExtents;

    // Hierarchy / Inspector が参照する選択添字。 Hierarchy クリックとギズモ選択の両方から更新する
    std::size_t m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
    // 選択中の area camera の cameraVolumes 添字。 オブジェクト選択とは排他
    std::size_t m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    // ビューポート由来のギズモ選択変化だけを index へ反映するための前フレーム値
    NS::Scene::Transform* m_lastGizmoSelected = nullptr;

    // Debug provenance パネルの読み出し元。 書き込みは Render で毎フレーム行う
    NS::Graphics::RenderSettings m_debugResolvedSettings{};
    NS::Graphics::RenderSettingsOverride m_debugSceneOverride{};
    NS::Graphics::RenderSettingsOverride m_debugPlayerObjectOverride{};
};

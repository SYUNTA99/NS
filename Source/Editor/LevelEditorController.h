#pragma once

/// @file LevelEditorController.h
/// @brief LevelPlayScene を編集する開発用コントローラ。Debug / Development 限定
///
/// @details scene 自身は「編集されている」ことを知らない。 本クラスが LevelPlayScene の公開 API を通して
/// runtime オブジェクト群 / camera / プレイ状態を操作し、 cursor / palette / undo の EditorMode ・
/// ギズモ変形・free-fly カメラ・編集 ↔ プレイのモード切替を実現する。 EditorLayer が所有し、
/// Setup / Tick / Render / Teardown を駆動する。 出荷 build には本クラスも EditorLayer も含めない

#include "Editor/EditorMode.h"
#include "Editor/GizmoEditor.h"
#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Math/Math.h"
#include "GameCore/Level/LevelData.h"
#include "GameCore/Level/PlayState.h"
#include "GameCore/Theme/ThemeId.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace NS::Scene
{
    class GameObject;
    class Transform;
    class CameraBrainComponent;
    class CameraComponent;
    class ThirdPersonFollowComponent;
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
    /// imgui は EditorLayer 所有の context。EditorMode / ギズモの入力ゲートへ非所有で橋渡しする
    void Setup(NS::UI::ImGuiContext* imgui);
    /// fixed step 更新。 編集中は free-fly カメラ / ギズモ / EditorMode を回す。 プレイ中はクリア監視のみ
    void Tick();
    /// ギズモ / palette / 編集ビジュアルといった render フレームの上乗せ描画と debug provenance 退避
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
    /// panel 利便のための pass-through。 編集対象の LevelData
    [[nodiscard]] NS::GameCore::Level::LevelData& Level() noexcept;
    /// panel 利便のための pass-through。 一時的な PlayState
    [[nodiscard]] NS::GameCore::Level::PlayState& Play() noexcept;

    /// ギズモ変形の Object ツールが有効か。 false はグリッド設置の Build
    [[nodiscard]] bool ObjectToolActive() const noexcept { return m_editorToolMode == EditorToolMode::Object; }
    /// 編集ツールを Build / Object 切替える。 Build へ戻す時はギズモ選択を解除する
    void SetObjectToolActive(bool active) noexcept;

    /// 現在選択中の配置物の objects 添字。 未選択 / 範囲外は kNoObjectIndex
    [[nodiscard]] std::size_t SelectedObjectIndex() const noexcept { return m_selectedObjectIndex; }
    /// Hierarchy から添字で配置物を選択する。 Object ツールへ切替え、 ギズモ選択も貼る
    void SelectObjectByIndex(std::size_t index) noexcept;

    /// Inspector が編集 / 表示できる選択を持つか
    [[nodiscard]] bool HasInspectableSelection() const noexcept;
    /// Inspector 表示用に選択中 ObjectInstance のコピーを返す。 未選択は既定値
    [[nodiscard]] NS::GameCore::Level::ObjectInstance SelectedObjectSnapshot() const noexcept;
    /// 選択中の配置物の位置を設定する。 非選択時は何もしない
    void SetSelectedFreePosition(NS::Math::Vector3 position) noexcept;
    /// 選択中の配置物の回転を設定する。 非選択時は何もしない
    void SetSelectedFreeRotation(NS::Math::Quaternion rotation) noexcept;
    /// 選択中の配置物のスケールを設定する。 最小正値に clamp する。 非選択時は何もしない
    void SetSelectedFreeScale(NS::Math::Vector3 scale) noexcept;
    /// 選択中自由オブジェクトの変形編集を開始し baseline を退避する。 gizmo ドラッグ / パネル入力の開始で呼ぶ
    void BeginTransformEdit() noexcept;
    /// 進行中の変形編集を 1 つの undo 単位として確定する。 無変化なら積まない
    void CommitTransformEdit() noexcept;

    /// 編集視点の中心あたりに新しい自由オブジェクトを 1 個追加して選択する。 Undo 対応
    void AddObject();

    /// 選択中の配置物に対応する runtime GameObject。 未選択 / 未構築は nullptr
    /// Inspector が Component の反射フィールドを描くのに使う。 player object は scene 所有の実 player を返す
    [[nodiscard]] NS::Scene::GameObject* SelectedObjectGameObject() noexcept;
    /// 選択中の配置物がプレイヤー実体か。 テンプレート保存の表示と複製禁止の判定に使う
    [[nodiscard]] bool SelectedIsPlayerObject() const noexcept;
    /// 選択中の配置物の runtime 全コンポーネントを components データへ書き戻し、 保存と rebuild に乗せる
    /// Inspector で反射編集した後に呼ぶ。 非選択時は何もしない
    void SyncSelectedObjectComponentsFromComponent();

    /// 選択 object の末尾へ型名のみのコンポーネントを足す。 Undo 対応、 非選択時は何もしない
    void AddComponentToSelected(std::string_view typeName);
    /// 選択 object の components から添字 1 つを取り除く。 Undo 対応、 範囲外 / 非選択時は何もしない
    void RemoveComponentFromSelected(std::size_t componentIndex);
    /// 選択 object を全コンポーネント込みで複製し複製を選択する。 Undo 対応、 非選択時は何もしない
    void DuplicateSelectedObject();
    /// 選択 object の指定添字コンポーネントを今のフィールド値ごと clipboard へ写す。 範囲外 / 非選択時は何もしない
    void CopyComponentToClipboard(std::size_t componentIndex);
    /// clipboard のコンポーネントを選択 object の末尾へ貼る。 Undo 対応、 clipboard 空 / 非選択時は何もしない
    /// 貼り付けても clipboard は残るので、 同じコンポを複数の object へ続けて貼れる
    void PasteClipboardComponentToSelected();
    /// clipboard にコンポーネントを保持しているか
    [[nodiscard]] bool HasClipboardComponent() const noexcept { return m_componentClipboard.has_value(); }

    /// CameraBrain を載せた GameObject。 未構築は nullptr。 Camera 選択時の Inspector 反射編集対象
    [[nodiscard]] NS::Scene::GameObject* CameraBrainObject() noexcept;
    /// 編集中=free-fly / プレイ中=follow の現在 active な仮想カメラの GameObject。 無ければ nullptr
    [[nodiscard]] NS::Scene::GameObject* ActiveVirtualCameraObject() noexcept;

    /// Hierarchy から Brain + active vcam の Camera を選択する。 配置物 / ギズモ選択は解除する
    void SelectCamera() noexcept;
    /// Inspector / Hierarchy が Camera 選択中か
    [[nodiscard]] bool IsCameraSelected() const noexcept { return m_specialSelection == SpecialSelection::Camera; }

    /// Object モードかつギズモで何か選択中なら true。 material 適用先がある状態
    [[nodiscard]] bool HasGizmoSelection() const noexcept
    {
        return ObjectToolActive() && m_gizmo.Selected() != nullptr;
    }
    /// ギズモ選択中の自由オブジェクトに matPath の .mat を適用する。 適用できたら true
    bool ApplyMaterialToSelected(const std::filesystem::path& matPath);

    /// `.asset` の雛形を読み直すだけ。 雛形はシーンの見た目に影響しないため world は組み直さない
    /// 編集した雛形をシーンに反映するには読み直したうえで ApplyTheme で適用し直す
    void ReloadThemes();

    /// 雛形 id の視覚値をシーンの環境欄へ写し込み、 slice 帯の焼き直しまで行う
    /// lighting / skybox は次フレームの設定写しで、 block の slice は RebuildWorld の焼き直しで反映される
    void ApplyTheme(NS::GameCore::Theme::ThemeId id);

    /// 環境の block slice 帯を変えて world を焼き直す。 lighting と違い帯変更だけが組み直しを要する
    void SetEnvironmentBlockSlice(std::uint16_t baseSlice);

    /// 最後に scene が解決した scene 段設定。 RenderSettings パネルの表示元
    [[nodiscard]] const NS::Graphics::RenderSettings& DebugResolvedSettings() const noexcept
    {
        return m_debugResolvedSettings;
    }
    /// 最後に構築した scene override。 default / scene の出所逆算の入力に使う
    [[nodiscard]] const NS::Graphics::RenderSettingsOverride& DebugSceneOverride() const noexcept
    {
        return m_debugSceneOverride;
    }
    /// Player の MeshRenderer である代表 object override。 object の出所逆算の入力
    [[nodiscard]] const NS::Graphics::RenderSettingsOverride& DebugPlayerObjectOverride() const noexcept
    {
        return m_debugPlayerObjectOverride;
    }

private:
    /// edit モードの fixed step: free-fly カメラ / ギズモ / EditorMode の入力処理と dirty rebuild
    void TickEdit();

    /// edit 中、 各カメラの視錐台を点線の四角錐で、 視点位置を小箱で DebugDraw で可視化する。 据え置きはトリガ AABB も
    void RenderCameraGizmos(const NS::Math::Matrix& viewProjection, NS::Math::Size2D viewport) noexcept;

    /// edit 中、 各オブジェクトの当たり形状を DebugDraw で可視化する。 Box は OBB / その他 collider は AABB
    void RenderColliderWireframes() noexcept;

    /// 全配置物のギズモ選択候補を作り直して注入する
    void RefreshGizmoSelectables();

    /// 選択 id から現在の runtime 実体を解決し、 派生添字の更新と gizmo への貼り直しを行う
    /// rebuild を跨いでも生ポインタを持ち越さない fail-safe の要。 ドラッグ中は gizmo 貼り直しを抑止する
    void ResolveSelectionFromId() noexcept;

    /// ギズモで変形した自由オブジェクトの Transform を対応する ObjectInstance へ書き戻す
    /// world に居ない実プレイヤーも player object のデータへ同様に書き戻す。 live Transform が真実の源
    void SyncFreeObjectTransforms();

    /// 選択中のオブジェクトが持つ追従カメラ component。 無ければ nullptr。 ギズモ逆算と undo 分岐が使う
    [[nodiscard]] NS::Scene::ThirdPersonFollowComponent* SelectedFollowCamera() noexcept;

    /// 追従カメラの Root を実プレイ視点位置へ同期する。 位置を持たない追従カメラを edit で掴めるようにする
    void SyncFollowCameraPoses();

    /// ドラッグ中の追従カメラの Root 位置から初期姿勢を逆算し、 components データへ書き戻す
    void ApplyFollowCameraGizmoDrag();

    /// ビューポートでギズモ選択が変わった時だけ、 選択 id と派生の添字を追従させる
    void CaptureSelectionFromGizmo() noexcept;

    /// 描画カメラの窓口。 scene の CameraSubsystem から brain を引く。 未登録は nullptr
    [[nodiscard]] NS::Scene::CameraBrainComponent* Brain() const noexcept;
    /// brain が駆動する実カメラ。 brain 未登録・実カメラ未解決は nullptr
    [[nodiscard]] NS::Scene::CameraComponent* MainCamera() const noexcept;

    /// 識別子でギズモ選択を貼り直す。 対象が消えていれば選択解除する
    void ReselectFreeObjectById(std::uint32_t id) noexcept;

    LevelPlayScene* m_scene = nullptr;

    // EditorLayer が所有する ImGui context を非所有で持つ。EditorMode / ギズモへ渡し、 keyboard キャプチャ判定にも使う
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
    // 各候補が可視メッシュを持つか。 見えないカメラ等が重なった可視ブロックの pick を奪わないための優先フラグ
    std::vector<std::uint8_t> m_selectablePickable;

    // 編集カメラのような配置物でない単一物の選択。 添字選択とは排他
    enum class SpecialSelection : std::uint8_t
    {
        None,
        Camera
    };
    SpecialSelection m_specialSelection = SpecialSelection::None;

    // 選択の真実は永続 objectId。 rebuild / delete / undo を跨いでも生ポインタや添字に依存しない
    std::uint32_t m_selectedObjectId = NS::GameCore::Level::kNoObjectId;
    // id から毎フレーム解決する派生の添字。 m_level.objects 用で、 ズレても crash しない安定 vector を指す
    std::size_t m_selectedObjectIndex = NS::GameCore::Level::kNoObjectIndex;
    // ビューポート由来のギズモ選択変化だけを index へ反映するための前フレーム値
    NS::Scene::Transform* m_lastGizmoSelected = nullptr;

    // コンポ単位 copy/paste の退避先。 型名 + 反射値を 1 つ保持する
    std::optional<NS::GameCore::Level::ComponentData> m_componentClipboard;

    // ギズモドラッグ / Inspector パネルの変形編集を 1 undo 単位へ束ねる状態
    bool m_gizmoWasDragging = false;
    bool m_transformEditing = false;
    std::uint32_t m_editBaselineId = NS::GameCore::Level::kNoObjectId;
    NS::GameCore::Level::ObjectInstance m_editBaseline{};

    // Debug provenance パネルの読み出し元。 書き込みは Render で毎フレーム行う
    NS::Graphics::RenderSettings m_debugResolvedSettings{};
    NS::Graphics::RenderSettingsOverride m_debugSceneOverride{};
    NS::Graphics::RenderSettingsOverride m_debugPlayerObjectOverride{};
};

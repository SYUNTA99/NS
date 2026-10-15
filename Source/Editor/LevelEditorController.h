#pragma once

#include "Editor/EditorCamera.h"
#include "Editor/EditorMode.h"
#include "Editor/EditorObjects.h"
#include "Editor/GizmoEditor.h"
#include "Editor/PlacementCatalog.h"
#include "Editor/PlayControls.h"
#include "Editor/Undo/ObjectSnapshotApplier.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/SubObjects/VirtualCamera.h"

#include "NSlib/Core/Types.h"
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    class SceneActor;
    class Transform;
    class CameraManager;
    class SceneCamera;
    class SubObject;
    class ActorList;
    class ThirdPersonFollow;
    class Scene;
    struct SceneView;
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
    namespace Gfx
    {
        class DebugShapes;
    }
} // namespace NS

//! シーンの編集操作と編集・プレイの切替を管理する
class LevelEditorController : public NS::NonCopyable
{
public:
    explicit LevelEditorController(NS::Scene* scene) noexcept;
    ~LevelEditorController();

    //! scene の OnStart 完了後に呼ぶ。自由視点カメラ / EditorMode / ギズモを立ち上げ編集モードへ入る
    void Setup(NS::UI::ImGuiContext* imgui);

    //! 固定ステップ更新。編集中は自由視点カメラ / ギズモ / EditorMode を回す。プレイ中は Esc
    //! で隠したカーソルを出すだけ
    void Tick();

    //! 編集中のカーソルの枠 / パレット / ギズモを ImGui のフレームへ重ねる。プレイ中は何もしない
    void Render();

    //! Scene ビューの図形を積む。編集時は選択中、プレイ時は全配置物の当たりを表示する
    //! shapes は空で渡され、ビューの描画後に破棄される。viewProjection はそのビューの行列
    void DrawSceneViewShapes(NS::Gfx::DebugShapes& shapes, const NS::Matrix& viewProjection) noexcept;

    //! scene 破棄の前に呼ぶ。ギズモ選択解除と自由視点カメラの後始末
    void Teardown();

    //! 編集モードかプレイモードか
    enum class Mode : NS::uint8
    {
        Edit,
        Play
    };

    [[nodiscard]] Mode CurrentMode() const noexcept { return m_mode; }

    //! 編集モードからプレイモードへ移行する
    void EnterPlay() noexcept;

    //! プレイモードから編集モードへ移行する
    void EnterEdit() noexcept;

    //! プレイ中に 1 固定ステップだけコマ送りする。自動で一時停止に入り、手触り検証で 1 コマずつ観察する
    void RequestStepFrame() noexcept;

    //! 前面のパネルが画像を描いたフレームで、矩形とカーソルの重なりを渡す
    void SetGameView(int x, int y, int width, int height, bool hovered) noexcept;
    //! F5 の全画面描画用。予備の矩形を使い、カーソルが重なっている状態にする
    void ClearGameView() noexcept;
    //! UI 表示中に前面のパネルが裏へ隠れたフレームで呼ぶ。編集入力とオーバーレイを止める
    void HideGameView() noexcept;
    //! 現在の表示矩形。未設定時は全画面の予備矩形を返す
    [[nodiscard]] NS::Editor::ViewRect CurrentViewRect() const noexcept;
    [[nodiscard]] bool GameViewHidden() const noexcept { return m_gameViewHidden; }

    //! プレイ中に Scene タブへ自由視点を映すフレームで毎回呼ぶ。入力を自由視点カメラへ流す
    //! 編集モード中は何もしない
    void TickPlaySceneView(const NS::Editor::EditorCameraInput& input) noexcept;

    //! @brief Scene パネルに映す視点。プレイ中は自由視点を上書き、編集中は CameraManager 任せ (nullopt)
    [[nodiscard]] std::optional<NS::CameraPose> SceneViewPose() noexcept;
    //! @brief Game パネルに映す視点。編集中はゲームカメラを上書き、プレイ中は CameraManager 任せ (nullopt)
    [[nodiscard]] std::optional<NS::CameraPose> GameViewPose() noexcept;
    //! @brief 可視な中央ビュー列を Scene へ流す。空なら現描画先へ 1 回だけ描く
    void SetSceneViews(std::vector<NS::SceneView> views);

    //! @brief 編集中の場面の写し (Scene::ToJson)。当たりの下見が写しから別の場面を組むのに読む
    //! @return 写し。場面が無ければ空の場面の文書
    [[nodiscard]] nlohmann::json SceneSnapshot() const;

    [[nodiscard]] NS::Editor::EditorMode& Editor() noexcept { return m_editor; }

    //! 編集の自由視点カメラ。Inspector が感度をライブ編集する
    [[nodiscard]] NS::Editor::EditorCamera& EditorFreeCamera() noexcept { return m_editorCamera; }

    //! プレイの時間停止。実体はシーンの時間停止スイッチで、ここはエディタ操作の入口
    [[nodiscard]] bool PlayPaused() const noexcept;
    void TogglePlayPause() noexcept;

    //! Game 画像の左クリックで視点操作を再開する。一時停止中か捕捉中なら何もしない
    void RecaptureCursorOnGameClick(bool gameImageClicked) noexcept;

    //! 編集対象の ActorList。一覧と参照候補はここから直接取得する
    [[nodiscard]] const NS::ActorList& Objects() const noexcept;

    [[nodiscard]] bool ObjectToolActive() const noexcept { return m_objectToolActive; }
    //! 配置ツールを Build / Object 切替える。Build へ戻す時はギズモ選択を解除する
    void SetObjectToolActive(bool active) noexcept;

    //! 現在選択中の配置物の表示用添字。選択の真実は永続 id で、添字は実体から引き直す
    [[nodiscard]] std::size_t SelectedObjectIndex() const noexcept;
    //! 永続 id で配置物を 1 体だけ選択する。0 を渡すと選択を外す
    void SelectObjectById(NS::uint32 id) noexcept;
    //! 現在の主対象の永続 id。未選択は 0。ギズモと Inspector はこの 1 体を見る
    [[nodiscard]] NS::uint32 SelectedObjectId() const noexcept { return m_selectedObjectId; }

    //! 選択中の全配置物の永続 id。主対象も含む
    [[nodiscard]] const std::vector<NS::uint32>& SelectedObjectIds() const noexcept { return m_selectionIds; }
    [[nodiscard]] bool IsObjectSelected(NS::uint32 id) const noexcept;
    //! 選択へ足す / 選択から外す。足した物が主対象になる
    void ToggleObjectSelection(NS::uint32 id) noexcept;
    //! 選択を差し替える。primary が ids に含まれていなければ追加する
    void SelectObjects(std::vector<NS::uint32> ids, NS::uint32 primary) noexcept;

    //! Inspector が編集 / 表示できる選択を持つか
    [[nodiscard]] bool HasInspectableSelection() const noexcept;

    //! 選択中の配置物の位置 / 回転 / スケールを実体へ直接設定する。非選択時は何もしない
    void SetSelectedFreePosition(NS::Vector3 position);
    void SetSelectedFreeRotation(NS::Quaternion rotation);
    void SetSelectedFreeScale(NS::Vector3 scale);

    //! ドラッグでの変形を始める / 確定する
    void BeginTransformEdit() noexcept;
    void CommitTransformEdit() noexcept;

    //! Inspector のリフレクション項目をドラッグで編集し始める / 確定する
    void BeginSubObjectEdit() noexcept;
    void CommitSubObjectEdit() noexcept;

    //! プレイ中の手編集を凍結スナップショットへも写す。編集復帰の組み直しを跨いで調整値が残る
    //! 編集モードでは実体が唯一の出所なので何もしない。Inspector の編集箇所と transform 設定子が呼ぶ
    void MirrorPlayEditToBaseline(const NS::SubObject& comp, std::string_view fieldName);

    //! 種類の既定値と未上書きの個体を更新する。変更できた場合 true、参照・姿勢など対象外の欄は false
    //! ファイル保存に失敗してもメモリ上の変更は残し、警告する。Undo の対象外
    bool PromoteFieldToArchetype(NS::SubObject& comp, std::string_view fieldName);

    //! 編集視点の中心付近へ配置し、選択と Undo 履歴へ追加する
    void PlaceItem(const NS::Editor::PlacementItem& item);

    //! @brief メッシュ資産を地形の部品として編集視点の中心あたりへ置く。Undo 対応
    //! @param[in] meshPath 資産ファイルの絶対パス。参照は ContentRoot 相対へ直して持つ
    void AddMeshParts(std::string_view meshPath);

    //! 選択中の配置物に対応する runtime SceneActor。未選択 / 未構築は nullptr
    [[nodiscard]] NS::SceneActor* SelectedObjectActor() noexcept;
    //! 選択中の配置物がプレイヤー実体か
    [[nodiscard]] bool SelectedIsPlayerObject() const noexcept;

    //! 改名して Undo 履歴へ追加する。不在なら何もしない。重複には番号を付け、空名は Object にする
    void RenameObject(NS::uint32 id, std::string_view name);

    //! 選択中の配置物を新しい永続 id で複製して undo へ積む。プレイヤーは複製の対象から外す
    void DuplicateSelectedObject();

    //! @brief 選択中の配置物を削除して取り消しの履歴へ 1 単位で積む
    //! @details プレイヤーを含む選択の場合は何もしない
    void DeleteSelectedObject();

    //! 編集カメラの注視点を選択中の配置物へ寄せる。広がりに応じて距離も取り直す
    void FocusSelectedInView() noexcept;

    //! Object ツール中にギズモが配置物を選択しているか
    [[nodiscard]] bool HasGizmoSelection() const noexcept
    {
        return ObjectToolActive() && m_gizmo.Selected() != nullptr;
    }
    //! 選択中の配置物の Model に材質を割り当てて undo へ積む。対象外なら false
    bool ApplyMaterialToSelected(std::string_view matPath);

private:
    void TickEdit();

    //! @brief プレイ中のカーソルを置き直す
    //! @details Captured は消して Game の画像の中心へ固定し、マウスを相対にする。Released はその逆
    void ApplyPlayCursor(NS::Editor::PlayCursor cursor) noexcept;

    //! プレイを終えて編集の姿へ戻す。凍結スナップショットから世界を組み直し、操作系を休止させ、カーソルを出す
    //! 編集モードでしか要らない遷移なのでエディタが持つ。組み直しで確保が起きるため noexcept にしない
    void LeavePlayForEdit();

    //! 配置物を新しい永続 id で 1 体追加する唯一の経路。採番・履歴登録・選択をまとめて面倒を見る
    void PushCreateObject(nlohmann::json object);

    //! 選んだ配置物の Root を書いた後、その欄をプレイ中の凍結へも写す
    void MirrorRootEditToBaseline(NS::SceneActor& actor, std::string_view fieldName);

    void RenderCameraGizmos(NS::Gfx::DebugShapes& shapes,
                            const NS::Matrix& viewProjection,
                            NS::Size2D viewport) noexcept;
    //! 当たり形状を積む。all が true なら全配置物、false なら選択分だけ
    void RenderCollisionWireframes(NS::Gfx::DebugShapes& shapes, bool all) noexcept;

    //! 判定と同じ面と命中点を描く。狙い対象は狙いの方向、他は自機から相手への水平方向を使う
    //! 自機がなければ編集カメラからの水平方向を使う
    void RenderHitFaces(NS::Gfx::DebugShapes& shapes) noexcept;

    //! 主対象以外の選択物を枠で見せる。ギズモは 1 体にしか出ないので、選んだ範囲を目で追えるようにする
    void RenderSelectionOutlines(NS::Gfx::DebugShapes& shapes) noexcept;
    void RefreshGizmoSelectables();
    void ResolveSelectionFromId() noexcept;

    //! 主対象だけを差し替える。選択集合には触らない
    void SetPrimarySelection(NS::uint32 id) noexcept;

    //! ドラッグ開始時に、主対象以外の選択物の世界変換を控える
    void CaptureDragFollowers() noexcept;
    //! 主対象がドラッグで動いた分を、控えた残りの選択へ同じだけ効かせる
    void ApplyDragToFollowers() noexcept;

    [[nodiscard]] NS::ThirdPersonFollow* SelectedFollowCamera() noexcept;
    void SyncFollowCameraPoses();
    void ApplyFollowCameraGizmoDrag();
    void CaptureSelectionFromGizmo() noexcept;

    [[nodiscard]] NS::CameraManager* Cameras() const noexcept;
    [[nodiscard]] NS::SceneCamera* MainCamera() const noexcept;

    NS::Scene* m_scene = nullptr;                // 編集対象のシーン。回す/止める/コマ送りもこのシーンのスイッチ
    NS::Editor::ObjectSnapshotApplier m_applier; // 履歴の操作を実体へ適用する

    // 前面のパネルの表示矩形。未設定時は CurrentViewRect が全画面の予備矩形を返す
    NS::Editor::ViewRect m_gameViewRect{};
    bool m_gameViewRectValid = false;
    bool m_gameViewHovered = false;
    bool m_gameViewHidden = false; // UI 表示中に前面のパネルが裏へ隠れているか

    NS::Editor::EditorCamera m_editorCamera;

    // 編集復帰時にプレイ視点から自由視点へ繋ぐブレンド。TickEdit が進める
    NS::CameraPose m_editBlendFrom{};
    float m_editBlendElapsed = 0.0f;
    bool m_editBlending = false;

    NS::Editor::EditorMode m_editor{};
    Mode m_mode = Mode::Edit;

    bool m_objectToolActive = true; // false の時は Build

    NS::Editor::GizmoEditor m_gizmo{};

    std::vector<NS::SceneActor*> m_selectablePtrs;
    std::vector<NS::uint8> m_selectablePickable; // 1 は Model を持つ配置物。ギズモが先に選ぶ

    NS::uint32 m_selectedObjectId = NS::k_NoObjectId; // 主対象の永続 id。選択の一次情報
    std::vector<NS::uint32> m_selectionIds;           // 選択中の全配置物。主対象も含む
    NS::Transform* m_lastGizmoSelected = nullptr;     // 前フレームの選択対象

    //! ドラッグ開始時点の姿。主対象の動きを同じだけ他へ流すための控え
    struct DragFollower
    {
        NS::uint32 id = 0;
        NS::Matrix world{};
    };
    std::vector<DragFollower> m_dragFollowers; // 主対象に付いて動く残りの選択
    NS::Matrix m_dragPrimaryWorld{};           // ドラッグ開始時の主対象の世界変換

    bool m_gizmoWasDragging = false;
    bool m_transformEditing = false;

    // 編集開始時の状態スナップショット。選択している分だけ並ぶ
    std::vector<std::pair<NS::uint32, nlohmann::json>> m_editBaselines;

    bool m_subObjectEditing = false;
    NS::uint32 m_subObjectEditBaselineId = NS::k_NoObjectId; // 編集開始時の対象 id
    nlohmann::json m_subObjectEditBaseline;                  // 編集開始時の状態スナップショット
};

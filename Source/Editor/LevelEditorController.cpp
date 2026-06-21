#include "Editor/LevelEditorController.h"

#include "Editor/EditorCameraRig.h"
#include "Game/Block.h"
#include "Game/LevelPlayScene.h"
#include "Game/Player.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Graphics/DebugDraw.h"
#include "Framework/Physics/SweptOBB.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/Components/CameraBrainComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PlacedVirtualCamera.h"
#include "Framework/Scene/Transform.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/LevelData.h"
#include "Game/Undo/AddObjectCommand.h"
#include "Game/Undo/TransformCommand.h"

#include <cmath>
#include <cstring>
#include <memory>

namespace
{
    constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};
} // namespace

LevelEditorController::LevelEditorController(LevelPlayScene* scene) noexcept : m_scene(scene) {}

LevelEditorController::~LevelEditorController() = default;

NS::Game::Level::LevelData& LevelEditorController::Level() noexcept
{
    return m_scene->Level();
}

NS::Game::Level::PlayState& LevelEditorController::Play() noexcept
{
    return m_scene->Play();
}

void LevelEditorController::Setup(NS::UI::ImGuiContext* imgui)
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr || m_scene == nullptr)
        return;

    m_imgui = imgui;

    // 編集モード専用の free-fly カメラを Player / follow camera と並列で立ち上げる
    // MB64 の freecam に相当 (mouse + gamepad で Orbit / Pan / Zoom)
    m_editorCameraRig = std::make_unique<EditorCameraRig>();
    m_editorCameraRig->AttachScene(m_scene);
    m_editorCameraRig->EditorCam().SetInput(&app->Input());

    // free-fly vcam の投影設定 (編集は遠景を 200 まで見せる、 near 0.1 は既定)
    m_editorCameraRig->EditorCam().SetNearPlane(0.1f);
    m_editorCameraRig->EditorCam().SetFarPlane(200.0f);
    m_editorCameraRig->EditorCam().SetFovY(NS::Math::ToRadians(NS::Math::Degrees{60.0f}));

    // 初期視点は spawn 位置を中心に少し引いた位置から見下ろす
    const NS::Math::Vector3 spawnPos{static_cast<float>(m_scene->m_level.spawnX),
                                     static_cast<float>(m_scene->m_level.spawnY),
                                     static_cast<float>(m_scene->m_level.spawnZ)};
    m_editorCameraRig->EditorCam().SetCenter(spawnPos);
    // 起動直後は spawn block を真ん中近めに見せる距離。 1m cube が画面の十数 % を占める
    m_editorCameraRig->EditorCam().SetDistance(5.0f);

    m_editorCameraRig->OnStart();

    // free-fly vcam を Brain へ登録する (follow / area camera は scene が登録済)
    if (m_scene->m_brain)
        m_scene->m_brain->AddVirtualCamera(&m_editorCameraRig->EditorCam());

    // EditorMode に依存先を注入する
    m_editor.SetLevel(&m_scene->m_level);
    m_editor.SetEditIds(&m_scene->m_objectIds, &m_scene->m_nextObjectId);
    m_editor.SetInput(&app->Input());
    m_editor.SetImGui(imgui);
    m_editor.SetCameraComponent(m_scene->m_mainCamera);
    m_editor.SetActive(true);
    // scene が OnStart で rebuild 済なので、 初回 Tick の二重 rebuild を抑制
    m_editor.ClearLevelDirty();

    // ギズモに依存先を注入する。 選択候補は自由オブジェクト + grid solid ブロックを連結して渡す
    m_gizmo.SetInput(&app->Input());
    m_gizmo.SetImGui(imgui);
    RefreshGizmoSelectables();

    // scene は OnStart でプレイ開始済。 編集モードへ切替える (player 凍結 / free-fly camera 有効)
    m_scene->SetPlaying(false);
    m_editorCameraRig->EditorCam().SetActive(true);
    m_mode = Mode::Edit;
}

void LevelEditorController::Teardown()
{
    // ギズモは free オブジェクトの Transform を非所有参照するので、 scene 破棄前に選択を外す
    m_gizmo.ClearSelection();
    if (m_editorCameraRig)
    {
        // Brain は free-fly vcam を非所有参照する。 vcam を畳む前に Brain から外して dangling を避ける
        if (m_scene != nullptr && m_scene->m_brain)
            m_scene->m_brain->RemoveVirtualCamera(&m_editorCameraRig->EditorCam());
        m_editorCameraRig->OnEndPlay();
    }
    m_editorCameraRig.reset();
    m_selectablePtrs.clear();
    m_selectableHalfExtents.clear();
}

void LevelEditorController::EnterPlay() noexcept
{
    if (m_mode == Mode::Play)
        return;
    m_mode = Mode::Play;
    // player spawn / 物理 / follow camera は scene が握る
    m_scene->SetPlaying(true);
    m_editor.SetActive(false);
    if (m_editorCameraRig)
        m_editorCameraRig->EditorCam().SetActive(false);
}

void LevelEditorController::EnterEdit() noexcept
{
    if (m_mode == Mode::Edit)
        return;
    m_mode = Mode::Edit;
    // player 凍結 / follow・area camera 休止 / play 状態リセットは scene が握る
    m_scene->SetPlaying(false);
    m_editor.SetActive(true);
    if (m_editorCameraRig)
        m_editorCameraRig->EditorCam().SetActive(true);
}

void LevelEditorController::Tick()
{
    if (m_mode == Mode::Edit)
    {
        TickEdit();
    }
    else if (m_scene->Play().clearTriggered)
    {
        // クリア成立で編集へ戻す (出荷にはこの controller が無いためクリア演出は別途必要)
        EnterEdit();
    }
}

void LevelEditorController::TickEdit()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // Esc: Object モードで選択中ならまず選択解除に使い終了させない
    if (app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
    {
        if (m_editorToolMode == EditorToolMode::Object && m_gizmo.Selected() != nullptr)
        {
            m_gizmo.ClearSelection();
            return;
        }
        NS::App::Application::Quit();
        return;
    }

    // free-fly camera を駆動
    if (m_editorCameraRig)
    {
        m_editorCameraRig->Root().Snapshot();
        m_editorCameraRig->OnUpdate();
    }

    // free-fly 更新後に実カメラへ反映し、 ギズモ / 編集の ray-pick が当フレームの視点を使えるようにする
    if (m_scene->m_brain)
        m_scene->m_brain->Evaluate(1.0f);

    // 同時に 1 モードだけが LMB/R/Ctrl+Z を消費する。 Object 中は grid 入力を抑制しギズモへ回す
    // モード切替は EditorLayer の UI ボタン (SetObjectToolActive) から行う (Tab は Edit↔Play 専用)
    const bool objectMode = (m_editorToolMode == EditorToolMode::Object);
    m_gizmo.SetActive(objectMode);
    m_editor.SetInputSuppressed(objectMode);

    if (objectMode && m_editorCameraRig && m_scene->m_brain)
    {
        const auto vp = m_scene->m_brain->ViewProjection();
        const auto viewport = app->Window().Size();
        const bool wasDragging = m_gizmoWasDragging;
        m_gizmo.Tick(vp, viewport);

        // 掴んだら自由化: 選択が grid solid ブロックなら自由 Transform オブジェクトへ昇格する
        if (NS::Scene::Transform* selected = m_gizmo.Selected())
        {
            for (std::size_t bi = 0; bi < m_scene->m_blocks.size(); ++bi)
            {
                if (m_scene->m_blocks[bi] && &m_scene->m_blocks[bi]->Root() == selected)
                {
                    PromoteGridBlockToFree(bi);
                    break;
                }
            }
        }

        // ビューポートでのギズモ選択変化を Hierarchy / Inspector の選択添字へ追従させる
        ResolveSelectedIndexFromGizmo();

        // ギズモ変形の結果を ObjectInstance へ反映 (live → model)。 begin/commit はこの model を基準にする
        SyncFreeObjectTransforms();

        // ドラッグ開始で baseline 退避、 終了で TransformCommand を 1 つ確定する (grid undo と同じ履歴)
        const bool nowDragging = m_gizmo.IsDragging();
        if (!wasDragging && nowDragging)
            BeginTransformEdit();
        else if (wasDragging && !nowDragging)
            CommitTransformEdit();
        m_gizmoWasDragging = nowDragging;
    }
    else if (m_transformEditing)
    {
        // Object モードを抜けても未確定の変形があれば確定する (silent な変更を残さない)
        CommitTransformEdit();
        m_gizmoWasDragging = false;
    }

    m_editor.Tick();
    if (m_editor.IsLevelDirty())
    {
        m_scene->RebuildBlocksFromLevelData();
        // ファイル読込で cameraVolumes が差し替わった場合に area camera を追従させる
        m_scene->RebuildAreaCamerasFromLevelData();
        // 作り直した runtime オブジェクトへギズモ選択候補を貼り直す (pointer dangling 防止)
        RefreshGizmoSelectables();
        // undo / redo / ロードで作り直した後、 ダングリングを避けるためギズモ選択を解除する
        m_gizmo.ClearSelection();
        m_editor.ClearLevelDirty();
    }
}

void LevelEditorController::Render()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // Debug provenance パネルの入力を毎フレーム退避する。出所は has_value の突き合わせで逆算するので
    // Resolve のホットパスに追跡を入れず、 scene の解決値と代表 object override をそのまま保持する
    m_debugResolvedSettings = m_scene->m_lastResolvedSettings;
    m_debugSceneOverride = m_scene->BuildSceneOverride();
    if (m_scene->m_player)
        m_debugPlayerObjectOverride = m_scene->m_player->MeshComp().RenderOverride();

    if (m_mode != Mode::Edit)
        return;

    m_editor.RenderSpawnMarker();
    m_editor.RenderCursorPreview();
    RenderAreaCameraGizmos();
    RenderColliderWireframes();
    // 蓄積した DebugDraw 線をシーン描画後・ ImGui 前にまとめて 1 描画する
    if (m_scene->m_brain)
        NS::Graphics::DebugDraw::Flush(app->Renderer(), m_scene->m_brain->ViewProjection());
    // Toolbar UI を ImGui 経由で描画 (Debug / Development build のみ実機能)
    m_editor.Palette().Render();
    // Object モードのギズモは最前面 (drawlist) に重ねる
    if (m_gizmo.IsActive() && m_scene->m_brain)
        m_gizmo.Render(m_scene->m_brain->ViewProjection(), app->Window().Size());
}

void LevelEditorController::SetObjectToolActive(bool active) noexcept
{
    const EditorToolMode next = active ? EditorToolMode::Object : EditorToolMode::Build;
    if (next == m_editorToolMode)
        return;
    m_editorToolMode = next;
    if (!active)
    {
        m_gizmo.ClearSelection();
        m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
        m_lastGizmoSelected = nullptr;
    }
}

bool LevelEditorController::HasInspectableSelection() const noexcept
{
    return m_selectedObjectIndex < m_scene->m_level.objects.size();
}

bool LevelEditorController::SelectedIsGridAligned() const noexcept
{
    return HasInspectableSelection() &&
           (m_scene->m_level.objects[m_selectedObjectIndex].flags & NS::Game::Level::kObjectFlagGridAligned) != 0;
}

NS::Game::Level::ObjectInstance LevelEditorController::SelectedObjectSnapshot() const noexcept
{
    if (m_selectedObjectIndex < m_scene->m_level.objects.size())
        return m_scene->m_level.objects[m_selectedObjectIndex];
    return NS::Game::Level::ObjectInstance{};
}

NS::Scene::GameObject* LevelEditorController::SelectedObjectGameObject() noexcept
{
    if (m_selectedObjectIndex >= m_scene->m_level.objects.size())
        return nullptr;

    // objects 添字 → runtime インスタンスの逆引き。 自由配置物 / grid solid のどちらかに居る
    for (std::size_t i = 0; i < m_scene->m_freeObjects.size() && i < m_scene->m_freeSourceIndices.size(); ++i)
        if (m_scene->m_freeSourceIndices[i] == m_selectedObjectIndex && m_scene->m_freeObjects[i])
            return m_scene->m_freeObjects[i].get();
    for (std::size_t i = 0; i < m_scene->m_blocks.size() && i < m_scene->m_blockSourceIndices.size(); ++i)
        if (m_scene->m_blockSourceIndices[i] == m_selectedObjectIndex && m_scene->m_blocks[i])
            return m_scene->m_blocks[i].get();
    return nullptr;
}

NS::Scene::GameObject* LevelEditorController::PlayerObject() noexcept
{
    return m_scene->m_player.get();
}

void LevelEditorController::SyncSelectedObjectColliderFromComponent() noexcept
{
    if (m_selectedObjectIndex >= m_scene->m_level.objects.size())
        return;

    // 反射編集で runtime collider の half-extents / offset / 回転は既に更新済。 それを ObjectInstance へ写して
    // 保存と次プレイの rebuild に乗せる (自由オブジェクトのみ。 grid は cell 固定で編集しない)
    for (std::size_t i = 0; i < m_scene->m_freeObjects.size() && i < m_scene->m_freeSourceIndices.size(); ++i)
    {
        if (m_scene->m_freeSourceIndices[i] != m_selectedObjectIndex || !m_scene->m_freeObjects[i])
            continue;
        const NS::Scene::StaticColliderComponent& collider = m_scene->m_freeObjects[i]->Collider();
        const NS::Math::Vector3 he = collider.HalfExtents();
        const NS::Math::Vector3 offset = collider.CenterOffset();
        const NS::Math::Quaternion rot = collider.LocalRotation();
        NS::Game::Level::ObjectInstance& object = m_scene->m_level.objects[m_selectedObjectIndex];
        object.colliderHalfExtentsX = he.x;
        object.colliderHalfExtentsY = he.y;
        object.colliderHalfExtentsZ = he.z;
        object.colliderOffsetX = offset.x;
        object.colliderOffsetY = offset.y;
        object.colliderOffsetZ = offset.z;
        object.colliderRotationX = rot.x;
        object.colliderRotationY = rot.y;
        object.colliderRotationZ = rot.z;
        object.colliderRotationW = rot.w;
        return;
    }
}

NS::Scene::GameObject* LevelEditorController::CameraBrainObject() noexcept
{
    return m_scene->m_brain ? m_scene->m_brain->Owner() : nullptr;
}

NS::Scene::GameObject* LevelEditorController::ActiveVirtualCameraObject() noexcept
{
    if (m_scene->m_brain == nullptr)
        return nullptr;
    NS::Scene::VirtualCameraComponent* active = m_scene->m_brain->ActiveVirtualCamera();
    return active ? active->Owner() : nullptr;
}

bool LevelEditorController::HasCameraSelection() const noexcept
{
    return m_selectedCameraIndex < m_scene->m_level.cameraVolumes.size();
}

void LevelEditorController::RefreshGizmoSelectables()
{
    m_selectablePtrs.clear();
    m_selectableHalfExtents.clear();
    m_selectablePtrs.reserve(m_scene->m_freeObjects.size() + m_scene->m_blocks.size());
    m_selectableHalfExtents.reserve(m_scene->m_freeObjects.size() + m_scene->m_blocks.size());

    // 自由オブジェクトは scale 付きなので pick box は halfExtents にスケールを乗せる
    for (const auto& obj : m_scene->m_freeObjects)
    {
        if (!obj)
            continue;
        const NS::Math::Vector3 scale = obj->Root().Scale();
        m_selectablePtrs.push_back(obj.get());
        m_selectableHalfExtents.push_back(NS::Math::Vector3{
            kCellHalfExtents.x * scale.x, kCellHalfExtents.y * scale.y, kCellHalfExtents.z * scale.z});
    }

    // grid solid ブロックも掴める。 掴むと PromoteGridBlockToFree で自由オブジェクトに変わる
    for (const auto& block : m_scene->m_blocks)
    {
        if (!block)
            continue;
        m_selectablePtrs.push_back(block.get());
        m_selectableHalfExtents.push_back(kCellHalfExtents);
    }

    m_gizmo.SetSelectableObjects(m_selectablePtrs, m_selectableHalfExtents);
}

void LevelEditorController::SyncFreeObjectTransforms()
{
    for (std::size_t i = 0; i < m_scene->m_freeObjects.size(); ++i)
    {
        if (!m_scene->m_freeObjects[i] || m_scene->m_freeSourceIndices[i] >= m_scene->m_level.objects.size())
            continue;

        const NS::Scene::Transform& root = m_scene->m_freeObjects[i]->Root();
        const NS::Math::Vector3 position = root.Position();
        const NS::Math::Quaternion rotation = root.Rotation();
        const NS::Math::Vector3 scale = root.Scale();

        NS::Game::Level::ObjectInstance& object = m_scene->m_level.objects[m_scene->m_freeSourceIndices[i]];
        object.positionX = position.x;
        object.positionY = position.y;
        object.positionZ = position.z;
        object.rotationX = rotation.x;
        object.rotationY = rotation.y;
        object.rotationZ = rotation.z;
        object.rotationW = rotation.w;
        object.scaleX = scale.x;
        object.scaleY = scale.y;
        object.scaleZ = scale.z;
    }
}

void LevelEditorController::SelectObjectByIndex(std::size_t index) noexcept
{
    // オブジェクトとカメラ / Player / Camera の選択は排他。 オブジェクトを選んだら他を解除する
    m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    m_specialSelection = SpecialSelection::None;

    if (index >= m_scene->m_level.objects.size())
    {
        m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
        return;
    }
    m_selectedObjectIndex = index;

    // ハンドルを出すため Object ツールへ切替える (Build のままだとギズモが描かれない)
    SetObjectToolActive(true);

    // 自由オブジェクトだけギズモへ貼る。 grid 配置物 (solid 含む) は一覧クリックでは昇格させず、
    // Inspector の Promote で明示的に自由化する (一覧での選択を非破壊に保つ)
    for (std::size_t i = 0; i < m_scene->m_freeSourceIndices.size(); ++i)
    {
        if (m_scene->m_freeSourceIndices[i] == index && m_scene->m_freeObjects[i])
        {
            m_gizmo.SetSelected(&m_scene->m_freeObjects[i]->Root());
            m_lastGizmoSelected = m_gizmo.Selected();
            return;
        }
    }
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::SelectCameraByIndex(std::size_t index) noexcept
{
    m_specialSelection = SpecialSelection::None;
    if (index >= m_scene->m_level.cameraVolumes.size())
    {
        m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
        return;
    }
    m_selectedCameraIndex = index;

    // カメラ選択中はオブジェクト / ギズモ選択を外す (Inspector はカメラを表示する)
    m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::SelectPlayer() noexcept
{
    // Player は gizmo 対象外。 添字 / ギズモ選択を外して特殊選択へ移す (ツールモードは触らない)
    m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
    m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
    m_specialSelection = SpecialSelection::Player;
}

void LevelEditorController::SelectCamera() noexcept
{
    m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
    m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
    m_specialSelection = SpecialSelection::Camera;
}

NS::Scene::PlacedVirtualCamera* LevelEditorController::SelectedAreaCamera() noexcept
{
    if (m_selectedCameraIndex >= m_scene->m_areaCameras.size())
        return nullptr;
    return m_scene->m_areaCameras[m_selectedCameraIndex].cam;
}

void LevelEditorController::SyncSelectedCameraVolumeFromComponent() noexcept
{
    if (m_selectedCameraIndex >= m_scene->m_level.cameraVolumes.size() ||
        m_selectedCameraIndex >= m_scene->m_areaCameras.size())
        return;
    NS::Scene::PlacedVirtualCamera* cam = m_scene->m_areaCameras[m_selectedCameraIndex].cam;
    if (cam == nullptr)
        return;

    // トリガ半径が 0 以下だと進入判定が常に外れるので最小正値に clamp し、 component 側へ反映する
    const NS::Math::Vector3 extent = cam->TriggerExtent();
    const NS::Math::Vector3 clamped{
        extent.x > 0.01f ? extent.x : 0.01f, extent.y > 0.01f ? extent.y : 0.01f, extent.z > 0.01f ? extent.z : 0.01f};
    if (clamped.x != extent.x || clamped.y != extent.y || clamped.z != extent.z)
        cam->SetTrigger(cam->TriggerCenter(), clamped);

    NS::Game::Level::CameraVolume& volume = m_scene->m_level.cameraVolumes[m_selectedCameraIndex];
    const NS::Math::Vector3& position = cam->ViewPosition();
    const NS::Math::Vector3& target = cam->ViewTarget();
    const NS::Math::Vector3& center = cam->TriggerCenter();
    const NS::Math::Vector3& finalExtent = cam->TriggerExtent();
    volume.cameraPositionX = position.x;
    volume.cameraPositionY = position.y;
    volume.cameraPositionZ = position.z;
    volume.lookTargetX = target.x;
    volume.lookTargetY = target.y;
    volume.lookTargetZ = target.z;
    volume.triggerCenterX = center.x;
    volume.triggerCenterY = center.y;
    volume.triggerCenterZ = center.z;
    volume.triggerExtentX = finalExtent.x;
    volume.triggerExtentY = finalExtent.y;
    volume.triggerExtentZ = finalExtent.z;
    volume.lookAtPlayer = cam->LooksAtPlayer() ? 1u : 0u;
    volume.priority = cam->VcamPriority();
}

void LevelEditorController::AddCameraVolume() noexcept
{
    // 新規カメラは編集視点の中心あたりに置き、 そこから少し引いた位置から中心を見るデフォルトにする
    NS::Math::Vector3 center{static_cast<float>(m_scene->m_level.spawnX),
                             static_cast<float>(m_scene->m_level.spawnY),
                             static_cast<float>(m_scene->m_level.spawnZ)};
    if (m_editorCameraRig)
        center = m_editorCameraRig->EditorCam().Center();

    NS::Game::Level::CameraVolume volume{};
    volume.cameraPositionX = center.x;
    volume.cameraPositionY = center.y + 5.0f;
    volume.cameraPositionZ = center.z - 10.0f;
    volume.lookTargetX = center.x;
    volume.lookTargetY = center.y;
    volume.lookTargetZ = center.z;
    volume.triggerCenterX = center.x;
    volume.triggerCenterY = center.y;
    volume.triggerCenterZ = center.z;
    volume.triggerExtentX = 3.0f;
    volume.triggerExtentY = 3.0f;
    volume.triggerExtentZ = 3.0f;
    volume.priority = 10;

    m_scene->m_level.cameraVolumes.push_back(volume);
    m_scene->RebuildAreaCamerasFromLevelData();
    SelectCameraByIndex(m_scene->m_level.cameraVolumes.size() - 1);
}

void LevelEditorController::DeleteSelectedCamera() noexcept
{
    if (m_selectedCameraIndex >= m_scene->m_level.cameraVolumes.size())
        return;
    m_scene->m_level.cameraVolumes.erase(m_scene->m_level.cameraVolumes.begin() +
                                         static_cast<std::ptrdiff_t>(m_selectedCameraIndex));
    m_selectedCameraIndex = NS::Game::Level::kNoObjectIndex;
    m_scene->RebuildAreaCamerasFromLevelData();
}

void LevelEditorController::RenderAreaCameraGizmos() noexcept
{
    // edit 中、 各 area camera のトリガ範囲 (AABB) とカメラ位置 → 注視点を線で可視化する
    // 選択中のカメラは強調色にする
    for (std::size_t i = 0; i < m_scene->m_level.cameraVolumes.size(); ++i)
    {
        const NS::Game::Level::CameraVolume& v = m_scene->m_level.cameraVolumes[i];
        const bool selected = (i == m_selectedCameraIndex);

        const NS::Math::Color triggerColor =
            selected ? NS::Math::Color{1.0f, 0.55f, 0.10f, 1.0f} : NS::Math::Color{0.20f, 0.70f, 1.0f, 1.0f};
        const NS::Math::AABB trigger{NS::Math::Vector3{v.triggerCenterX, v.triggerCenterY, v.triggerCenterZ},
                                     NS::Math::Vector3{v.triggerExtentX, v.triggerExtentY, v.triggerExtentZ}};
        NS::Graphics::DebugDraw::AABB(trigger, triggerColor);

        const NS::Math::Vector3 camPos{v.cameraPositionX, v.cameraPositionY, v.cameraPositionZ};
        const NS::Math::Vector3 lookAt{v.lookTargetX, v.lookTargetY, v.lookTargetZ};
        const NS::Math::Color camColor{1.0f, 0.85f, 0.10f, 1.0f};
        const NS::Math::AABB camMarker{camPos, NS::Math::Vector3{0.3f, 0.3f, 0.3f}};
        NS::Graphics::DebugDraw::AABB(camMarker, camColor);
        NS::Graphics::DebugDraw::Line(camPos, lookAt, camColor);
    }
}

void LevelEditorController::RenderColliderWireframes() noexcept
{
    // 自由配置物は回転・スケール込みの OBB、 grid solid は AABB で当たり形状を可視化する
    // 両者を緑系で出し、 自由配置=明るい緑 / grid=濃い緑 で区別する
    const NS::Math::Color freeColor{0.35f, 1.0f, 0.45f, 1.0f};
    const NS::Math::Color gridColor{0.15f, 0.70f, 0.30f, 1.0f};

    for (const auto& obj : m_scene->m_freeObjects)
    {
        if (!obj)
            continue;
        const NS::Physics::OBB obb = obj->Collider().WorldOBB();
        NS::Graphics::DebugDraw::OBB(obb.center, obb.axisX, obb.axisY, obb.axisZ, obb.halfExtents, freeColor);
    }
    for (const auto& block : m_scene->m_blocks)
    {
        if (!block)
            continue;
        NS::Graphics::DebugDraw::AABB(block->Collider().WorldAABB(), gridColor);
    }
}

void LevelEditorController::ResolveSelectedIndexFromGizmo() noexcept
{
    NS::Scene::Transform* selected = m_gizmo.Selected();
    // 前フレームと同じなら据え置き (Hierarchy で選んだ非選択候補を毎フレーム潰さないため)
    if (selected == m_lastGizmoSelected)
        return;
    m_lastGizmoSelected = selected;
    if (selected == nullptr)
    {
        m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
        return;
    }
    // ビューポートでの実ピックは Player / Camera の特殊選択より優先する
    m_specialSelection = SpecialSelection::None;
    for (std::size_t i = 0; i < m_scene->m_freeObjects.size(); ++i)
    {
        if (m_scene->m_freeObjects[i] && &m_scene->m_freeObjects[i]->Root() == selected)
        {
            m_selectedObjectIndex = m_scene->m_freeSourceIndices[i];
            return;
        }
    }
    for (std::size_t i = 0; i < m_scene->m_blocks.size(); ++i)
    {
        if (m_scene->m_blocks[i] && &m_scene->m_blocks[i]->Root() == selected)
        {
            m_selectedObjectIndex = m_scene->m_blockSourceIndices[i];
            return;
        }
    }
    m_selectedObjectIndex = NS::Game::Level::kNoObjectIndex;
}

void LevelEditorController::SetSelectedFreePosition(NS::Math::Vector3 position) noexcept
{
    for (std::size_t i = 0; i < m_scene->m_freeSourceIndices.size(); ++i)
    {
        if (m_scene->m_freeSourceIndices[i] == m_selectedObjectIndex && m_scene->m_freeObjects[i])
        {
            m_scene->m_freeObjects[i]->Root().SetPosition(position);
            return;
        }
    }
}

void LevelEditorController::SetSelectedFreeRotation(NS::Math::Quaternion rotation) noexcept
{
    // 永続化は gizmo R と同じく SyncFreeObjectTransforms 経由 (runtime Transform を真実の源にする)
    for (std::size_t i = 0; i < m_scene->m_freeSourceIndices.size(); ++i)
    {
        if (m_scene->m_freeSourceIndices[i] == m_selectedObjectIndex && m_scene->m_freeObjects[i])
        {
            m_scene->m_freeObjects[i]->Root().SetRotation(rotation);
            return;
        }
    }
}

void LevelEditorController::SetSelectedFreeScale(NS::Math::Vector3 scale) noexcept
{
    // ImGui の入力で 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
    constexpr float kMinScale = 0.01f;
    scale.x = scale.x < kMinScale ? kMinScale : scale.x;
    scale.y = scale.y < kMinScale ? kMinScale : scale.y;
    scale.z = scale.z < kMinScale ? kMinScale : scale.z;
    for (std::size_t i = 0; i < m_scene->m_freeSourceIndices.size(); ++i)
    {
        if (m_scene->m_freeSourceIndices[i] == m_selectedObjectIndex && m_scene->m_freeObjects[i])
        {
            m_scene->m_freeObjects[i]->Root().SetScale(scale);
            return;
        }
    }
}

void LevelEditorController::PromoteSelectedToFree() noexcept
{
    for (std::size_t i = 0; i < m_scene->m_blockSourceIndices.size(); ++i)
    {
        if (m_scene->m_blockSourceIndices[i] == m_selectedObjectIndex && m_scene->m_blocks[i])
        {
            PromoteGridBlockToFree(i);
            return;
        }
    }
}

void LevelEditorController::AddObject()
{
    // 新規オブジェクトは編集視点の中心あたりへ置く (Add Camera と同じ基準点)
    NS::Math::Vector3 center{static_cast<float>(m_scene->m_level.spawnX),
                             static_cast<float>(m_scene->m_level.spawnY),
                             static_cast<float>(m_scene->m_level.spawnZ)};
    if (m_editorCameraRig)
        center = m_editorCameraRig->EditorCam().Center();

    // flags は 0 のまま = 非 gridAligned (自由配置物)。 scale / rotation / collider は既定値
    NS::Game::Level::ObjectInstance object{};
    object.positionX = center.x;
    object.positionY = center.y;
    object.positionZ = center.z;
    object.kind = NS::Game::Blocks::kBlockIdSolid;

    // grid 設置と同じ undo 履歴へ載せる。 Do が objects / ids 末尾へ append する
    NS::Game::Undo::EditTarget target = SceneEditTarget();
    m_editor.Undo().Push(std::make_unique<NS::Game::Undo::AddObjectCommand>(object), target);

    // 追加した自由オブジェクトの runtime 実体を作り、 選択候補を貼り直して末尾 (新規) を選択する
    m_scene->RebuildBlocksFromLevelData();
    RefreshGizmoSelectables();
    SelectObjectByIndex(m_scene->m_level.objects.size() - 1);
}

void LevelEditorController::PromoteGridBlockToFree(std::size_t blockIndex)
{
    if (blockIndex >= m_scene->m_blocks.size() || !m_scene->m_blocks[blockIndex])
        return;

    // 選択された grid block の cell を求め、 対応する ObjectInstance の gridAligned を落として自由化する
    const NS::Math::Vector3 worldPos = m_scene->m_blocks[blockIndex]->Root().Position();
    const std::int16_t cx = static_cast<std::int16_t>(std::lround(worldPos.x));
    const std::int16_t cy = static_cast<std::int16_t>(std::lround(worldPos.y));
    const std::int16_t cz = static_cast<std::int16_t>(std::lround(worldPos.z));

    const std::size_t objectIndex = NS::Game::Level::FindGridObjectAtCell(m_scene->m_level, cx, cy, cz);
    if (objectIndex == NS::Game::Level::kNoObjectIndex)
        return;

    // 昇格 (gridAligned を落とす) を TransformCommand 1 つとして積み、 grid undo と同じ履歴へ載せる
    const std::uint32_t id = m_scene->m_objectIds[objectIndex];
    const NS::Game::Level::ObjectInstance before = m_scene->m_level.objects[objectIndex];
    NS::Game::Level::ObjectInstance after = before;
    after.flags &= static_cast<std::uint8_t>(~NS::Game::Level::kObjectFlagGridAligned);
    NS::Game::Undo::EditTarget target = SceneEditTarget();
    m_editor.Undo().Push(std::make_unique<NS::Game::Undo::TransformCommand>(id, before, after), target);

    // 作り直すと自由化した object は m_freeObjects 側へ回る。 選択候補 span も貼り直す
    m_scene->RebuildBlocksFromLevelData();
    RefreshGizmoSelectables();
    ReselectFreeObjectById(id);
}

NS::Game::Undo::EditTarget LevelEditorController::SceneEditTarget() noexcept
{
    return NS::Game::Undo::EditTarget{m_scene->m_level, m_scene->m_objectIds, m_scene->m_nextObjectId};
}

void LevelEditorController::BeginTransformEdit() noexcept
{
    if (m_transformEditing)
        return;
    if (m_selectedObjectIndex >= m_scene->m_level.objects.size())
        return;
    m_editBaseline = m_scene->m_level.objects[m_selectedObjectIndex];
    m_editBaselineId = m_scene->m_objectIds[m_selectedObjectIndex];
    m_transformEditing = true;
}

void LevelEditorController::CommitTransformEdit() noexcept
{
    if (!m_transformEditing)
        return;
    m_transformEditing = false;

    NS::Game::Undo::EditTarget target = SceneEditTarget();
    const std::size_t index = NS::Game::Undo::IndexOfId(target, m_editBaselineId);
    if (index == NS::Game::Level::kNoObjectIndex)
        return;

    // 非 PRS (kind / flags / material) は model から、 PRS は live Transform から取る
    // (パネル編集は Sync が 1 フレーム遅れるため model 直読みだと取りこぼす)
    NS::Game::Level::ObjectInstance after = m_scene->m_level.objects[index];
    for (std::size_t i = 0; i < m_scene->m_freeSourceIndices.size(); ++i)
    {
        if (m_scene->m_freeSourceIndices[i] == index && m_scene->m_freeObjects[i])
        {
            const NS::Scene::Transform& root = m_scene->m_freeObjects[i]->Root();
            const NS::Math::Vector3 p = root.Position();
            const NS::Math::Quaternion r = root.Rotation();
            const NS::Math::Vector3 s = root.Scale();
            after.positionX = p.x;
            after.positionY = p.y;
            after.positionZ = p.z;
            after.rotationX = r.x;
            after.rotationY = r.y;
            after.rotationZ = r.z;
            after.rotationW = r.w;
            after.scaleX = s.x;
            after.scaleY = s.y;
            after.scaleZ = s.z;
            break;
        }
    }

    if (std::memcmp(&after, &m_editBaseline, sizeof(NS::Game::Level::ObjectInstance)) == 0)
        return;

    // model を after に確定してから push する。 Push の Do は model == after なので no-op で履歴記録のみ
    m_scene->m_level.objects[index] = after;
    m_editor.Undo().Push(std::make_unique<NS::Game::Undo::TransformCommand>(m_editBaselineId, m_editBaseline, after),
                         target);
}

void LevelEditorController::ReselectFreeObjectById(std::uint32_t id) noexcept
{
    NS::Game::Undo::EditTarget target = SceneEditTarget();
    const std::size_t index = NS::Game::Undo::IndexOfId(target, id);
    for (std::size_t i = 0; i < m_scene->m_freeSourceIndices.size(); ++i)
    {
        if (m_scene->m_freeSourceIndices[i] == index && m_scene->m_freeObjects[i])
        {
            m_gizmo.SetSelected(&m_scene->m_freeObjects[i]->Root());
            return;
        }
    }
    m_gizmo.ClearSelection();
}

bool LevelEditorController::ApplyMaterialToSelected(const std::filesystem::path& matPath)
{
    if (m_editorToolMode != EditorToolMode::Object || !m_scene->m_materialLibrary)
        return false;

    NS::Scene::Transform* selected = m_gizmo.Selected();
    if (selected == nullptr)
        return false;

    // 選択中の Transform を持つ自由オブジェクトを探す (ギズモ選択は自由オブジェクトを指す)
    std::size_t freeSlot = m_scene->m_freeObjects.size();
    for (std::size_t i = 0; i < m_scene->m_freeObjects.size(); ++i)
    {
        if (m_scene->m_freeObjects[i] && &m_scene->m_freeObjects[i]->Root() == selected)
        {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot >= m_scene->m_freeObjects.size())
        return false;

    const auto loaded = m_scene->m_materialLibrary->Load(matPath);
    if (loaded.material == nullptr)
        return false;

    // .mat パスを ContentRoot 相対で材質表に登録 (重複は再利用) し、 ObjectInstance.materialIndex を更新して永続化する
    const auto exeDir = NS::Core::FileSystem::ContentRoot();
    const std::filesystem::path relative = matPath.lexically_relative(exeDir);
    const std::string stored = relative.empty() ? matPath.generic_string() : relative.generic_string();

    int materialIndex = -1;
    for (std::size_t k = 0; k < m_scene->m_level.materialPaths.size(); ++k)
    {
        if (m_scene->m_level.materialPaths[k] == stored)
        {
            materialIndex = static_cast<int>(k);
            break;
        }
    }
    if (materialIndex < 0)
    {
        m_scene->m_level.materialPaths.push_back(stored);
        materialIndex = static_cast<int>(m_scene->m_level.materialPaths.size() - 1);
    }
    m_scene->m_level.objects[m_scene->m_freeSourceIndices[freeSlot]].materialIndex =
        static_cast<std::int16_t>(materialIndex);

    m_scene->m_freeObjects[freeSlot]->MeshComp().SetMaterial(loaded.material);
    m_scene->m_freeObjects[freeSlot]->MeshComp().SetBaseColor(loaded.baseColor);
    return true;
}

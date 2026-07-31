#include "Editor/EditorMode.h"

#include "Editor/EditorObjects.h"
#include "Editor/EditorUi.h"
#include "Editor/GridMath.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/IObjectSnapshotApplier.h"
#include "Editor/Undo/ObjectSnapshotCommand.h"
#include "Game/Level/FollowCameraObject.h"
#include "Game/Level/KillZoneComponent.h"
#include "Game/Player.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Math/Math.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Gamepad.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"
#include "Runtime/Platform/Mouse.h"
#include "Runtime/UI/ImGuiContext.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

#include <limits>
#include <optional>

namespace NS::Editor
{
    namespace
    {
        constexpr float k_CellHalfExtent = 0.5f;

        // 90度（ラジアン）の定数
        constexpr float k_QuarterTurnYaw = NS::Math::k_Pi * 0.5f;

        constexpr NS::Math::Color k_CursorOkColor{0.1f, 1.0f, 0.1f, 1.0f};
        constexpr NS::Math::Color k_CursorBlockedColor{1.0f, 0.1f, 0.1f, 1.0f};

        constexpr float k_StatusToastSeconds = 2.5f;

        [[nodiscard]] std::int16_t RoundToCell(float v) noexcept
        {
            return static_cast<std::int16_t>(std::lround(v));
        }

        // current + delta を 4 で割った余り。 90° 刻み回転を wrap する
        [[nodiscard]] std::uint8_t RotateMod4(std::uint8_t current, std::int8_t delta) noexcept
        {
            int32_t r = static_cast<int32_t>(current) + delta;
            r = ((r % 4) + 4) % 4;
            return static_cast<std::uint8_t>(r);
        }
    } // namespace

    bool EditorMode::HasObjectAtCell(std::int16_t x, std::int16_t y, std::int16_t z) const noexcept
    {
        return m_findCellObject && m_findCellObject(x, y, z) != NS::Object::k_NoObjectId;
    }

    void EditorMode::Tick() noexcept
    {
        if (!m_active)
        {
            return;
        }

        UpdateCursorFromInput();
        HandlePlaceDeleteInput();
        HandleRotationInput();
        HandleUndoRedoInput();
        HandleSaveLoadInput();
        m_palette.TickInput(m_input, m_imgui);

        // カーソルの回転状態に合わせて、表示用のヨー角を滑らかに追従させる
        const auto targetQuat = NS::Math::Quaternion::CreateFromAxisAngle(
            {0.0f, 1.0f, 0.0f}, static_cast<float>(m_currentRotation) * k_QuarterTurnYaw);
        constexpr float k_RotationSpringRate = 12.0f;
        const float dt = NS::Core::FrameTimer::FixedDelta();
        const float t = std::min(1.0f, k_RotationSpringRate * dt);
        m_displayedYawQuat = NS::Math::Quaternion::Slerp(m_displayedYawQuat, targetQuat, t);
    }

    void EditorMode::HandleSaveLoadInput() noexcept
    {
        if (m_input == nullptr)
        {
            return;
        }

        const bool wantKb = (m_imgui != nullptr) && m_imgui->WantCaptureKeyboard();
        if (wantKb)
        {
            return;
        }

        const auto& kb = m_input->Keyboard();
        if (!kb.IsHeld(NS::Platform::Key::Ctrl))
        {
            return;
        }

        const bool shift = kb.IsHeld(NS::Platform::Key::Shift);
        if (kb.IsPressed(NS::Platform::Key::S))
        {
            // Shift 付きは常に名前を付けて保存、 素の Ctrl+S は上書き (名前が無ければ付けて保存へ落ちる)
            if (shift)
            {
                OpenSaveModal();
            }
            else
            {
                RequestSave();
            }
        }
        if (kb.IsPressed(NS::Platform::Key::O))
        {
            OpenLoadModal();
        }
    }

    void EditorMode::RequestSave() noexcept
    {
        if (m_currentLevelName.empty())
        {
            OpenSaveModal();
        }
        else
        {
            OverwriteCurrentLevel();
        }
    }

    bool EditorMode::SaveLevelToName(std::string_view name) noexcept
    {
        if (!m_captureLevel)
        {
            return false;
        }

        const auto safe = SanitizeLevelName(name);
        const auto path = BuildLevelPath(safe);
        if (safe.empty() || !path)
        {
            return false;
        }

        (void)EnsureScenesDirectoryExists();

        // 保存の出所は live 実体。 捕捉関数で実体から SceneData を起こして書く
        const NS::Object::SceneData snapshot = m_captureLevel();
        const bool ok = NS::Object::SaveSceneToJsonFile(snapshot, *path);
        if (ok)
        {
            m_currentLevelName = safe;
            m_savedUndoVersion = m_undo.Version();
        }
        return ok;
    }

    void EditorMode::OverwriteCurrentLevel() noexcept
    {
        const bool ok = SaveLevelToName(m_currentLevelName);
        const char* prefix = "保存失敗: ";
        if (ok)
        {
            prefix = "上書き保存: ";
        }

        m_statusMessage = prefix + m_currentLevelName;
        m_statusError = !ok;
        m_statusTimer = k_StatusToastSeconds;
    }

    bool EditorMode::SaveForQuit() noexcept
    {
        // 名前付きレベルは従来どおり上書きする
        if (!m_currentLevelName.empty())
        {
            return SaveLevelToName(m_currentLevelName);
        }

        // 起動レベルが実在するのに読めず seed で立ち上がった時は、 元ファイルを潰さないよう退避名へ逃がす
        if (m_bootLevelLoadFailed)
        {
            const bool ok = SaveLevelToName("recovered_level");
            const char* prefix = "退避保存失敗: ";
            if (ok)
            {
                prefix = "退避保存: ";
            }
            m_statusMessage = std::string(prefix) + "recovered_level";
            m_statusError = !ok;
            m_statusTimer = k_StatusToastSeconds;
            return ok;
        }

        // 通常起動 / 新規は同梱シーンへ書き戻す
        return SaveLevelToName("new_scene");
    }

    void EditorMode::RenderFileBrowser() noexcept
    {
        const auto result = m_fileBrowser.Render();
        switch (result.action)
        {
        case LevelFileBrowser::Action::RequestSave:
        {
            const bool ok = SaveLevelToName(result.targetName);
            const char* message = "保存失敗";
            if (ok)
            {
                message = "保存成功";
            }
            m_fileBrowser.NotifySaveResult(ok, message);
            break;
        }
        case LevelFileBrowser::Action::RequestLoad:
        {
            auto path = BuildLevelPath(result.targetName);
            if (!path || !m_loadLevel)
            {
                m_fileBrowser.NotifyLoadResult(false, "不正な level name");
                break;
            }

            NS::Object::SceneData fresh;
            const bool ok = NS::Object::LoadSceneFromJsonFile(fresh, *path);
            if (ok)
            {
                // プレイヤー / 追従カメラ / 落下死の受け皿が欠けたレベルには既定の 1 体を補う
                // 追従カメラの Target にプレイヤーの id が要るので、 揃える順はこの並びで決まる
                (void)EnsurePlayerObject(fresh);
                (void)NS::Game::Level::EnsureFollowCameraObject(fresh, PlayerObjectId(fresh));
                (void)NS::Game::Level::EnsureKillZoneObject(fresh);

                // 読込済みデータを実体側へ取り込み world を組み直す。 新レベルなので Undo 履歴もクリアする
                m_loadLevel(std::move(fresh));
                m_undo.Clear();
                m_savedUndoVersion = m_undo.Version();
                m_levelDirty = true;
                m_currentLevelName = result.targetName;
                m_fileBrowser.NotifyLoadResult(true, "読込成功");
            }
            else
            {
                m_fileBrowser.NotifyLoadResult(false, "読込失敗 (破損 or version 不一致)");
            }
            break;
        }
        case LevelFileBrowser::Action::None:
        default:
            break;
        }

#if NS_EDITOR_ENABLED
        // モーダル外での操作結果（上書き保存など）をトースト通知として描画する
        if (m_statusTimer > 0.0f)
        {
            m_statusTimer -= NS::Core::FrameTimer::DeltaSeconds();
            const auto vp = ImGui::GetMainViewport();
            if (vp != nullptr)
            {
                ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 12.0f),
                                        ImGuiCond_Always,
                                        ImVec2(0.5f, 0.0f));
                ImGui::SetNextWindowBgAlpha(0.75f);

                constexpr ImGuiWindowFlags k_SaveToastFlags =
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;

                if (ImGui::Begin("##save_toast", nullptr, k_SaveToastFlags))
                {
                    const ImVec4 color = [this]() -> ImVec4 {
                        if (m_statusError)
                            return k_MsgErrorColor;
                        return k_MsgOkColor;
                    }();
                    ImGui::TextColored(color, "%s", m_statusMessage.c_str());
                }
                ImGui::End();
            }
        }
#endif
    }

    void EditorMode::RenderCursorPreview() noexcept
    {
        if (!m_active || !m_cursor.valid)
        {
            return;
        }

        const NS::Math::AABB placeBox(m_cursor.placementCenter,
                                      NS::Math::Vector3{k_CellHalfExtent, k_CellHalfExtent, k_CellHalfExtent});
        NS::Math::Color cursorColor = k_CursorOkColor;

        if (m_cursor.placementBlocked)
        {
            cursorColor = k_CursorBlockedColor;
        }

        NS::Graphics::DebugDraw::AABB(placeBox, cursorColor);

#if NS_EDITOR_ENABLED
        if (m_camera == nullptr)
        {
            return;
        }

        const ViewRect view = m_viewRect;
        if (view.width <= 0 || view.height <= 0)
        {
            return;
        }

        const auto vp = m_camera->ViewProjection();
        const NS::Math::Vector3 c = m_cursor.placementCenter;
        constexpr float h = k_CellHalfExtent;
        const float vpW = static_cast<float>(view.width);
        const float vpH = static_cast<float>(view.height);
        const float originX = static_cast<float>(view.x);
        const float originY = static_cast<float>(view.y);

        // 中央に Game 窓が立つため背景 drawlist では窓の裏に隠れる。前面へ描き、パネル外はクリップで漏らさない
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (dl == nullptr)
        {
            return;
        }
        dl->PushClipRect(ImVec2{originX, originY}, ImVec2{originX + vpW, originY + vpH}, true);

        const auto project = [&](const NS::Math::Vector3& world, ImVec2& out) -> bool {
            const NS::Math::Vector4 worldH{world.x, world.y, world.z, 1.0f};
            const NS::Math::Vector4 clip = NS::Math::Vector4::Transform(worldH, vp);
            if (clip.w <= 0.0f)
            {
                return false;
            }
            out.x = originX + ((clip.x / clip.w) * 0.5f + 0.5f) * vpW;
            out.y = originY + (1.0f - ((clip.y / clip.w) * 0.5f + 0.5f)) * vpH;
            return true;
        };

        // 選択セルの境界ボックスを描画する
        const NS::Math::Vector3 boxCorners[8] = {
            {c.x - h, c.y - h, c.z - h},
            {c.x + h, c.y - h, c.z - h},
            {c.x + h, c.y + h, c.z - h},
            {c.x - h, c.y + h, c.z - h},
            {c.x - h, c.y - h, c.z + h},
            {c.x + h, c.y - h, c.z + h},
            {c.x + h, c.y + h, c.z + h},
            {c.x - h, c.y + h, c.z + h},
        };
        ImVec2 boxScreen[8]{};
        bool boxFront[8]{};
        for (int i = 0; i < 8; ++i)
        {
            boxFront[i] = project(boxCorners[i], boxScreen[i]);
        }

        static constexpr int k_BoxEdges[12][2] = {
            {0, 1},
            {1, 2},
            {2, 3},
            {3, 0},
            {4, 5},
            {5, 6},
            {6, 7},
            {7, 4},
            {0, 4},
            {1, 5},
            {2, 6},
            {3, 7},
        };

        const ImU32 boxColor = [this]() -> ImU32 {
            if (m_cursor.placementBlocked)
            {
                return IM_COL32(255, 64, 64, 255);
            }
            return IM_COL32(64, 255, 64, 255);
        }();

        for (const auto& e : k_BoxEdges)
        {
            if (boxFront[e[0]] && boxFront[e[1]])
            {
                dl->AddLine(boxScreen[e[0]], boxScreen[e[1]], boxColor, 2.0f);
            }
        }

        // スロープが選択されている場合、向きを視覚化するためにウェッジ形状を描画する
        const float slopeAngle = m_palette.CurrentSlopeAngleDegrees();
        if (slopeAngle >= 0.0f)
        {
            const float angle = slopeAngle;
            const float rawHeight = std::tan(NS::Math::DegreesToRadians(angle)) * (2.0f * h);
            const float height = std::min(rawHeight, 2.0f * h);
            const float yBot = -h;
            const float yTop = -h + height;

            const NS::Math::Vector3 wedgeLocal[6] = {
                {-h, yBot, -h},
                {+h, yBot, -h},
                {-h, yBot, +h},
                {+h, yBot, +h},
                {-h, yTop, +h},
                {+h, yTop, +h},
            };

            ImVec2 wedgeScreen[6]{};
            bool wedgeFront[6]{};
            for (int i = 0; i < 6; ++i)
            {
                const auto r = NS::Math::Vector3::Transform(wedgeLocal[i], m_displayedYawQuat);
                wedgeFront[i] = project(NS::Math::Vector3{c.x + r.x, c.y + r.y, c.z + r.z}, wedgeScreen[i]);
            }

            static constexpr int k_WedgeEdges[9][2] = {
                {0, 1},
                {0, 2},
                {1, 3},
                {2, 3},
                {0, 4},
                {1, 5},
                {4, 5},
                {2, 4},
                {3, 5},
            };

            const ImU32 slopeColor = IM_COL32(150, 255, 210, 230);
            for (const auto& e : k_WedgeEdges)
            {
                if (wedgeFront[e[0]] && wedgeFront[e[1]])
                {
                    dl->AddLine(wedgeScreen[e[0]], wedgeScreen[e[1]], slopeColor, 1.0f);
                }
            }
        }

        dl->PopClipRect();
#endif
    }

    void EditorMode::PlaceUnderCursorProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_applier == nullptr || !m_findCellObject || !m_allocateId)
        {
            return;
        }

        // 回転対象外は回転 step を 0 に倒す
        const std::uint8_t rotation = [this]() -> std::uint8_t {
            if (m_palette.CurrentIsRotatable())
            {
                return m_currentRotation;
            }
            return std::uint8_t{0};
        }();

        // パレット雛形を cell 座標と回転 step だけ焼いて 1 体分の姿を作る
        NS::Object::ObjectData placed = m_palette.CurrentTemplate();
        NS::Object::SetObjectPosition(
            placed, NS::Math::Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        NS::Editor::SetCellRotationStep(placed, rotation);

        // 既存 cell は同じ永続 id で置換、 空 cell は新規採番
        std::optional<NS::Object::ObjectData> before;
        std::uint32_t id = m_findCellObject(x, y, z);
        if (id != NS::Object::k_NoObjectId)
        {
            before = m_applier->CaptureObject(id);
        }
        else
        {
            id = m_allocateId();
        }
        placed.objectId = id;

        m_undo.Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(placed)),
                    *m_applier);
        m_levelDirty = true;
    }

    void EditorMode::DeleteAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_applier == nullptr || !m_findCellObject)
        {
            return;
        }

        const std::uint32_t id = m_findCellObject(x, y, z);
        if (id == NS::Object::k_NoObjectId)
        {
            return;
        }
        std::optional<NS::Object::ObjectData> before = m_applier->CaptureObject(id);
        if (!before)
        {
            return;
        }
        m_undo.Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::nullopt),
                    *m_applier);
        m_levelDirty = true;
    }

    void EditorMode::RotateAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_applier == nullptr || !m_findCellObject)
        {
            return;
        }

        const std::uint32_t id = m_findCellObject(x, y, z);
        if (id == NS::Object::k_NoObjectId)
        {
            return;
        }
        std::optional<NS::Object::ObjectData> before = m_applier->CaptureObject(id);
        if (!before || !NS::Editor::IsRotatableObject(*before))
        {
            return;
        }
        NS::Object::ObjectData after = *before;
        const std::uint8_t step = NS::Editor::CellRotationStep(*before);
        NS::Editor::SetCellRotationStep(after, RotateMod4(step, std::int8_t{1}));
        m_undo.Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(*before), std::move(after)),
                    *m_applier);
        m_levelDirty = true;
    }

    void EditorMode::UpdateCursorFromInput() noexcept
    {
        m_cursor = CursorState{};

        if (m_inputSuppressed)
        {
            return;
        }
        if (m_input == nullptr || m_camera == nullptr)
        {
            return;
        }

        // Game パネル外のマウスから配置レイを飛ばさない
        if (!m_viewHovered)
        {
            return;
        }
        const ViewRect view = m_viewRect;
        if (view.width <= 0 || view.height <= 0)
        {
            return;
        }
        int mouseX = 0;
        int mouseY = 0;
        WindowMouseToViewSpace(m_input->Mouse().GetX(), m_input->Mouse().GetY(), mouseX, mouseY);
        if (!ViewRectContains(view, mouseX, mouseY))
        {
            return;
        }
        int localX = 0;
        int localY = 0;
        ViewRectToLocal(view, mouseX, mouseY, localX, localY);

        const auto vp = m_camera->ViewProjection();
        const NS::Math::Ray ray = NS::Editor::ScreenToWorldRay(vp, ViewRectSize(view), localX, localY);

        float bestT = std::numeric_limits<float>::max();
        bool hit = false;
        std::int16_t hitX = 0;
        std::int16_t hitY = 0;
        std::int16_t hitZ = 0;
        NS::Math::Vector3 hitPoint{};
        NS::Math::Vector3 hitNormal{0.0f, 1.0f, 0.0f};

        std::vector<CellCoord> cells;
        if (m_collectCells)
        {
            cells = m_collectCells();
        }
        for (const CellCoord& cell : cells)
        {
            const std::int16_t cx = cell.x;
            const std::int16_t cy = cell.y;
            const std::int16_t cz = cell.z;
            const NS::Math::Vector3 center{static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cz)};
            const NS::Math::AABB box(center, {k_CellHalfExtent, k_CellHalfExtent, k_CellHalfExtent});

            float t = 0.0f;
            if (ray.Intersects(box, t) && t < bestT)
            {
                bestT = t;
                hit = true;
                hitX = cx;
                hitY = cy;
                hitZ = cz;
                hitPoint = NS::Math::Vector3(ray.position.x + ray.direction.x * t,
                                             ray.position.y + ray.direction.y * t,
                                             ray.position.z + ray.direction.z * t);

                const NS::Math::Vector3 d = hitPoint - center;
                const float ax = std::fabs(d.x);
                const float ay = std::fabs(d.y);
                const float az = std::fabs(d.z);

                if (ax > ay && ax > az)
                {
                    float signX = -1.0f;
                    if (d.x > 0.0f)
                    {
                        signX = 1.0f;
                    }
                    hitNormal = NS::Math::Vector3{signX, 0.0f, 0.0f};
                }
                else if (ay > az)
                {
                    float signY = -1.0f;
                    if (d.y > 0.0f)
                    {
                        signY = 1.0f;
                    }
                    hitNormal = NS::Math::Vector3{0.0f, signY, 0.0f};
                }
                else
                {
                    float signZ = -1.0f;
                    if (d.z > 0.0f)
                    {
                        signZ = 1.0f;
                    }
                    hitNormal = NS::Math::Vector3{0.0f, 0.0f, signZ};
                }
            }
        }

        if (hit)
        {
            // ヒットしたセルの法線方向から、隣接する配置先セルを算出する
            const std::int16_t normalX = static_cast<std::int16_t>(std::lround(hitNormal.x));
            const std::int16_t normalY = static_cast<std::int16_t>(std::lround(hitNormal.y));
            const std::int16_t normalZ = static_cast<std::int16_t>(std::lround(hitNormal.z));
            const std::int16_t placeX = static_cast<std::int16_t>(hitX + normalX);
            const std::int16_t placeY = static_cast<std::int16_t>(hitY + normalY);
            const std::int16_t placeZ = static_cast<std::int16_t>(hitZ + normalZ);

            m_cursor.valid = true;
            m_cursor.hitX = hitX;
            m_cursor.hitY = hitY;
            m_cursor.hitZ = hitZ;
            m_cursor.deleteCenter =
                NS::Math::Vector3{static_cast<float>(hitX), static_cast<float>(hitY), static_cast<float>(hitZ)};
            m_cursor.placementCenter =
                NS::Math::Vector3{static_cast<float>(placeX), static_cast<float>(placeY), static_cast<float>(placeZ)};
            m_cursor.placeX = placeX;
            m_cursor.placeY = placeY;
            m_cursor.placeZ = placeZ;
            m_cursor.hitNormal = hitNormal;
            m_cursor.placementBlocked = HasObjectAtCell(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
            return;
        }

        NS::Math::Vector3 cellCenter{};
        if (!NS::Editor::TryGroundPlaneFallback(ray, cellCenter))
        {
            return;
        }

        m_cursor.valid = true;
        m_cursor.placementCenter = cellCenter;
        m_cursor.placeX = RoundToCell(cellCenter.x);
        m_cursor.placeY = RoundToCell(cellCenter.y);
        m_cursor.placeZ = RoundToCell(cellCenter.z);
        m_cursor.hitX = m_cursor.placeX;
        m_cursor.hitY = m_cursor.placeY;
        m_cursor.hitZ = m_cursor.placeZ;
        m_cursor.hitNormal = NS::Math::Vector3{0.0f, 1.0f, 0.0f};
        m_cursor.deleteCenter = cellCenter;
        m_cursor.placementBlocked = HasObjectAtCell(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
    }

    void EditorMode::HandlePlaceDeleteInput() noexcept
    {
        // Game 窓の上では ImGui が常にマウスを要求するため、UI 上かどうかの判定は
        // カーソルの有効性 (パネル hover + 矩形内) に任せる
        if (m_input == nullptr || !m_cursor.valid)
        {
            return;
        }
        if (m_inputSuppressed)
        {
            return;
        }

        auto& mouse = m_input->Mouse();
        if (mouse.IsPressed(NS::Platform::MouseButton::Left) && !m_cursor.placementBlocked)
        {
            PlaceUnderCursorProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
        }
        if (mouse.IsPressed(NS::Platform::MouseButton::Right) &&
            HasObjectAtCell(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            DeleteAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
        }

        auto& gp = m_input->Gamepad(0);
        if (!gp.IsConnected())
        {
            return;
        }

        if (gp.IsPressed(NS::Platform::GamepadButton::A) && !m_cursor.placementBlocked)
        {
            PlaceUnderCursorProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
        }
        if (gp.IsPressed(NS::Platform::GamepadButton::B) &&
            HasObjectAtCell(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            DeleteAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
        }
    }

    void EditorMode::HandleRotationInput() noexcept
    {
        if (m_input == nullptr)
        {
            return;
        }
        if (m_imgui != nullptr && m_imgui->WantCaptureKeyboard())
        {
            return;
        }
        if (m_inputSuppressed)
        {
            return;
        }

        const bool rotate =
            m_input->Keyboard().IsPressed(NS::Platform::Key::R) ||
            (m_input->Gamepad(0).IsConnected() && m_input->Gamepad(0).IsPressed(NS::Platform::GamepadButton::Y));

        if (!rotate || !m_cursor.valid)
        {
            return;
        }

        if (HasObjectAtCell(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            // 回転対象かどうかは RotateAtProgrammatic が捕捉した姿で判定する
            RotateAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
        }
        else if (m_palette.CurrentIsRotatable())
        {
            m_currentRotation = static_cast<std::uint8_t>((m_currentRotation + 1) & 0x03);
        }
    }

    void EditorMode::HandleUndoRedoInput() noexcept
    {
        if (m_input == nullptr || m_applier == nullptr)
        {
            return;
        }
        // ドラッグ中は選択の貼り直しを飛ばすため、 旧 Transform を指したまま undo が走らないように止める
        if (m_undoRedoSuppressed)
        {
            return;
        }
        if (m_imgui != nullptr && m_imgui->WantCaptureKeyboard())
        {
            return;
        }

        auto& kb = m_input->Keyboard();
        const bool ctrl = kb.IsHeld(NS::Platform::Key::Ctrl);
        const bool shift = kb.IsHeld(NS::Platform::Key::Shift);

        if (ctrl && shift && kb.IsPressed(NS::Platform::Key::Z))
        {
            PerformRedo();
            return;
        }
        if (ctrl && kb.IsPressed(NS::Platform::Key::Z))
        {
            PerformUndo();
        }
        if (ctrl && kb.IsPressed(NS::Platform::Key::Y))
        {
            PerformRedo();
        }
    }

    bool EditorMode::PerformUndo() noexcept
    {
        if (m_applier == nullptr || !m_undo.Undo(*m_applier))
        {
            return false;
        }
        m_levelDirty = true;
        return true;
    }

    bool EditorMode::PerformRedo() noexcept
    {
        if (m_applier == nullptr || !m_undo.Redo(*m_applier))
        {
            return false;
        }
        m_levelDirty = true;
        return true;
    }
} // namespace NS::Editor
#include "Editor/EditorMode.h"

#include "Editor/EditorObjects.h"
#include "Editor/EditorUi.h"
#include "Editor/GridMath.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/IObjectSnapshotApplier.h"
#include "Editor/Undo/ObjectSnapshotCommand.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Object/Scene/SceneCamera.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/UI/ImGuiContext.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Gamepad.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Keyboard.h"
#include "NSlib/Windows/Mouse.h"

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

        constexpr NS::Color k_CursorOkColor{0.1f, 1.0f, 0.1f, 1.0f};
        constexpr NS::Color k_CursorBlockedColor{1.0f, 0.1f, 0.1f, 1.0f};

        constexpr float k_StatusToastSeconds = 2.5f;

        [[nodiscard]] std::int16_t RoundToCell(float v) noexcept
        {
            return static_cast<std::int16_t>(std::lround(v));
        }

        //! ワールド座標をパネル上のスクリーン座標へ射影する。カメラの後ろ (w <= 0) なら false を返す
        struct CursorScreenProjector
        {
            NS::Matrix viewProjection; // 射影に使う view * projection
            float originX = 0.0f;      // パネル左上の X
            float originY = 0.0f;      // パネル左上の Y
            float width = 0.0f;        // パネル幅
            float height = 0.0f;       // パネル高さ

            bool operator()(const NS::Vector3& world, ImVec2& out) const noexcept
            {
                NS::Vector2 pixel{};
                float w = 0.0f;
                if (!NS::Gfx::TryProjectToPixels(viewProjection, world, width, height, pixel, w))
                {
                    return false;
                }
                out.x = originX + pixel.x;
                out.y = originY + pixel.y;
                return true;
            }
        };
    } // namespace

    bool EditorMode::HasObjectAtCell(std::int16_t x, std::int16_t y, std::int16_t z) const noexcept
    {
        return m_findCellObject && m_findCellObject(x, y, z) != NS::Obj::k_NoObjectId;
    }

    bool EditorMode::IsCellOccupied(std::int16_t x, std::int16_t y, std::int16_t z) const noexcept
    {
        return m_cellOccupied && m_cellOccupied(x, y, z);
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
        const NS::Quaternion targetQuat = RotationToQuaternion(m_currentRotation);
        constexpr float k_RotationSpringRate = 12.0f;
        const float dt = NS::OS::FrameTimer::FixedDelta();
        const float t = std::min(1.0f, k_RotationSpringRate * dt);
        m_displayedYawQuat = NS::Quaternion::Slerp(m_displayedYawQuat, targetQuat, t);
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

        const NS::OS::Keyboard& kb = m_input->Keyboard();
        if (!kb.IsHeld(NS::OS::Key::Ctrl))
        {
            return;
        }

        const bool shift = kb.IsHeld(NS::OS::Key::Shift);
        if (kb.IsPressed(NS::OS::Key::S))
        {
            // Shift 付きは常に名前を付けて保存、素の Ctrl+S は上書き (名前が無ければ付けて保存へ落ちる)
            if (shift)
            {
                OpenSaveModal();
            }
            else
            {
                RequestSave();
            }
        }
        if (kb.IsPressed(NS::OS::Key::O))
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
        // 休止中はプレイ中。live はプレイで動いた後の姿で、編集の内容ではないので書かない
        if (!m_active || !m_captureLevel)
        {
            return false;
        }

        // 素の名前は Scenes/ 配下に直してから控える。保存先と現在名の指すファイルを一致させる
        const std::string safe = QualifyLevelPath(SanitizeLevelPath(name));
        const std::optional<std::string> path = BuildLevelPath(safe);
        if (safe.empty() || !path)
        {
            return false;
        }

        (void)EnsureScenesDirectoryExists();

        // 保存の出所は live 実体。捕捉関数で実体からシーンの JSON 文書を作って書く
        const nlohmann::json snapshot = m_captureLevel();
        const bool ok = NS::Obj::SaveSceneToJsonFile(snapshot, *path);
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

        // 起動レベルが実在するのに読めず seed で立ち上がった時は、元ファイルを潰さないよう退避名へ逃がす
        if (m_bootLevelLoadFailed)
        {
            const bool ok = SaveLevelToName("Scenes/recovered_level");
            const char* prefix = "退避保存失敗: ";
            if (ok)
            {
                prefix = "退避保存: ";
            }
            m_statusMessage = std::string(prefix) + "Scenes/recovered_level";
            m_statusError = !ok;
            m_statusTimer = k_StatusToastSeconds;
            return ok;
        }

        // 通常起動 / 新規は同梱シーンへ書き戻す
        return SaveLevelToName("Scenes/new_scene");
    }

    void EditorMode::RenderFileBrowser() noexcept
    {
        const LevelFileBrowser::Result result = m_fileBrowser.Render();
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
            std::optional<std::string> path = BuildLevelPath(result.targetName);
            if (!path || !m_loadLevel)
            {
                m_fileBrowser.NotifyLoadResult(false, "不正な level name");
                break;
            }

            nlohmann::json fresh;
            const bool ok = NS::Obj::LoadSceneFromJsonFile(fresh, *path);
            if (ok)
            {
                // プレイヤー / 落下死体積が欠けたレベルには既定の 1 体を補う
                (void)EnsurePlayerObject(fresh);
                (void)EnsureDeathZoneObject(fresh);

                // 読込済みデータを実体側へ取り込み world を組み直す。新レベルなので Undo 履歴もクリアする
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
        // 上書き保存などモーダルを開かずに終わった操作をトーストで知らせる
        if (m_statusTimer > 0.0f)
        {
            m_statusTimer -= NS::OS::FrameTimer::DeltaSeconds();
            ImGuiViewport* const vp = ImGui::GetMainViewport();
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
                        {
                            return k_MsgErrorColor;
                        }
                        return k_MsgOkColor;
                    }();
                    ImGui::TextColored(color, "%s", m_statusMessage.c_str());
                }
                ImGui::End();
            }
        }
#endif
    }

    void EditorMode::DrawCursorShapes(NS::Gfx::DebugShapes& shapes) const noexcept
    {
        if (!m_active || !m_cursor.valid)
        {
            return;
        }

        const NS::AABB placeBox(m_cursor.placementCenter,
                                NS::Vector3{k_CellHalfExtent, k_CellHalfExtent, k_CellHalfExtent});
        NS::Color cursorColor = k_CursorOkColor;

        if (m_cursor.placementBlocked)
        {
            cursorColor = k_CursorBlockedColor;
        }

        shapes.AABB(placeBox, cursorColor);
    }

    void EditorMode::RenderCursorPreview() noexcept
    {
        if (!m_active || !m_cursor.valid)
        {
            return;
        }

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

        const NS::Matrix vp = m_camera->ViewProjection();
        const NS::Vector3 c = m_cursor.placementCenter;
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

        const CursorScreenProjector project{vp, originX, originY, vpW, vpH};

        // 選択セルの境界ボックスを描画する
        const NS::Vector3 boxCorners[8] = {
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

        for (const int(&e)[2] : k_BoxEdges)
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
            const float rawHeight = std::tan(NS::DegreesToRadians(angle)) * (2.0f * h);
            const float height = std::min(rawHeight, 2.0f * h);
            const float yBot = -h;
            const float yTop = -h + height;

            const NS::Vector3 wedgeLocal[6] = {
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
                const NS::Vector3 r = NS::Vector3::Transform(wedgeLocal[i], m_displayedYawQuat);
                wedgeFront[i] = project(NS::Vector3{c.x + r.x, c.y + r.y, c.z + r.z}, wedgeScreen[i]);
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
            for (const int(&e)[2] : k_WedgeEdges)
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

        // 回転対象外は回転 step を 0 にする
        const std::uint8_t rotation = [this]() -> std::uint8_t {
            if (m_palette.CurrentIsRotatable())
            {
                return m_currentRotation;
            }
            return std::uint8_t{0};
        }();

        // パレット雛形を cell 座標と回転 step だけ書き込んで 1 体分の姿を作る
        nlohmann::json placed = m_palette.CurrentTemplate();
        NS::Obj::SetObjectPosition(placed,
                                   NS::Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        NS::Editor::SetCellRotationStep(placed, rotation);

        // 既存 cell は同じ永続 id で置換、空 cell は新規採番
        std::optional<nlohmann::json> before;
        std::uint32_t id = m_findCellObject(x, y, z);
        if (id != NS::Obj::k_NoObjectId)
        {
            before = m_applier->CaptureObject(id);
        }
        else if (IsCellOccupied(x, y, z))
        {
            // 地形の部品でない物は筆で置き換えない
            return;
        }
        else
        {
            id = m_allocateId();
        }
        NS::Obj::SetObjectJsonId(placed, id);

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
        if (id == NS::Obj::k_NoObjectId)
        {
            return;
        }
        std::optional<nlohmann::json> before = m_applier->CaptureObject(id);
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
        if (id == NS::Obj::k_NoObjectId)
        {
            return;
        }
        std::optional<nlohmann::json> before = m_applier->CaptureObject(id);
        if (!before)
        {
            return;
        }
        nlohmann::json after = *before;
        NS::Editor::AddCellQuarterTurn(after);
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

        const NS::Matrix vp = m_camera->ViewProjection();
        const NS::Ray ray = NS::Editor::ScreenToWorldRay(vp, ViewRectSize(view), localX, localY);

        float bestT = std::numeric_limits<float>::max();
        bool hit = false;
        CellCoord hitCell{};
        CellCoord normal{0, 1, 0};
        NS::Vector3 hitPoint{};

        std::vector<CellCoord> cells;
        if (m_collectCells)
        {
            cells = m_collectCells();
        }
        for (const CellCoord& cell : cells)
        {
            const NS::Vector3 center{
                static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z)};
            const NS::AABB box(center, {k_CellHalfExtent, k_CellHalfExtent, k_CellHalfExtent});

            float t = 0.0f;
            if (ray.Intersects(box, t) && t < bestT)
            {
                bestT = t;
                hit = true;
                hitCell = cell;
                hitPoint = NS::Vector3(ray.position.x + ray.direction.x * t,
                                       ray.position.y + ray.direction.y * t,
                                       ray.position.z + ray.direction.z * t);

                const NS::Vector3 d = hitPoint - center;
                const float ax = std::fabs(d.x);
                const float ay = std::fabs(d.y);
                const float az = std::fabs(d.z);

                normal = CellCoord{};
                if (ax > ay && ax > az)
                {
                    normal.x = -1;
                    if (d.x > 0.0f)
                    {
                        normal.x = 1;
                    }
                }
                else if (ay > az)
                {
                    normal.y = -1;
                    if (d.y > 0.0f)
                    {
                        normal.y = 1;
                    }
                }
                else
                {
                    normal.z = -1;
                    if (d.z > 0.0f)
                    {
                        normal.z = 1;
                    }
                }
            }
        }

        if (hit)
        {
            const CellCoord place{static_cast<std::int16_t>(hitCell.x + normal.x),
                                  static_cast<std::int16_t>(hitCell.y + normal.y),
                                  static_cast<std::int16_t>(hitCell.z + normal.z)};

            m_cursor.valid = true;
            m_cursor.hit = hitCell;
            m_cursor.placementCenter =
                NS::Vector3{static_cast<float>(place.x), static_cast<float>(place.y), static_cast<float>(place.z)};
            m_cursor.place = place;
            m_cursor.placementBlocked = IsCellOccupied(place.x, place.y, place.z);
            return;
        }

        NS::Vector3 cellCenter{};
        if (!NS::Editor::TryGroundPlaneFallback(ray, cellCenter))
        {
            return;
        }

        m_cursor.valid = true;
        m_cursor.placementCenter = cellCenter;
        m_cursor.place = CellCoord{RoundToCell(cellCenter.x), RoundToCell(cellCenter.y), RoundToCell(cellCenter.z)};
        m_cursor.hit = m_cursor.place;
        m_cursor.placementBlocked = IsCellOccupied(m_cursor.place.x, m_cursor.place.y, m_cursor.place.z);
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

        NS::OS::Mouse& mouse = m_input->Mouse();
        if (mouse.IsPressed(NS::OS::MouseButton::Left) && !m_cursor.placementBlocked)
        {
            PlaceUnderCursorProgrammatic(m_cursor.place.x, m_cursor.place.y, m_cursor.place.z);
        }
        if (mouse.IsPressed(NS::OS::MouseButton::Right) &&
            HasObjectAtCell(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z))
        {
            DeleteAtProgrammatic(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z);
        }

        NS::OS::Gamepad& gp = m_input->Gamepad();
        if (!gp.IsConnected())
        {
            return;
        }

        if (gp.IsPressed(NS::OS::GamepadButton::A) && !m_cursor.placementBlocked)
        {
            PlaceUnderCursorProgrammatic(m_cursor.place.x, m_cursor.place.y, m_cursor.place.z);
        }
        if (gp.IsPressed(NS::OS::GamepadButton::B) && HasObjectAtCell(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z))
        {
            DeleteAtProgrammatic(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z);
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
            m_input->Keyboard().IsPressed(NS::OS::Key::R) ||
            (m_input->Gamepad().IsConnected() && m_input->Gamepad().IsPressed(NS::OS::GamepadButton::Y));

        if (!rotate || !m_cursor.valid)
        {
            return;
        }

        if (HasObjectAtCell(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z))
        {
            RotateAtProgrammatic(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z);
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
        // ドラッグ中は選択の貼り直しを飛ばすため、旧 Transform を指したまま undo が走らないように止める
        if (m_undoRedoSuppressed)
        {
            return;
        }
        if (m_imgui != nullptr && m_imgui->WantCaptureKeyboard())
        {
            return;
        }

        NS::OS::Keyboard& kb = m_input->Keyboard();
        const bool ctrl = kb.IsHeld(NS::OS::Key::Ctrl);
        const bool shift = kb.IsHeld(NS::OS::Key::Shift);

        if (ctrl && shift && kb.IsPressed(NS::OS::Key::Z))
        {
            PerformRedo();
            return;
        }
        if (ctrl && kb.IsPressed(NS::OS::Key::Z))
        {
            PerformUndo();
        }
        if (ctrl && kb.IsPressed(NS::OS::Key::Y))
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
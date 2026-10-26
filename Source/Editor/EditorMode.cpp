#include "Editor/EditorMode.h"
#include "NSlib/Core/Types.h"

#include "Editor/EditorActors.h"
#include "Editor/EditorUi.h"
#include "Editor/GridMath.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/CompositeCommand.h"
#include "Editor/Undo/IActorSnapshotApplier.h"
#include "Editor/Undo/ActorSnapshotCommand.h"
#include "Game/Game.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Core/Windows/Clock.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Gamepad.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/Keyboard.h"
#include "NSlib/Core/Windows/Mouse.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>

namespace NS
{
    namespace Editor
    {
        namespace
        {
            constexpr float k_CellHalfExtent = 0.5f;

            constexpr Color k_CursorOkColor{0.1f, 1.0f, 0.1f, 1.0f};
            constexpr Color k_CursorBlockedColor{1.0f, 0.1f, 0.1f, 1.0f};

            constexpr float k_StatusToastSeconds = 2.5f;

            [[nodiscard]] int16 RoundToCell(float v) noexcept
            {
                return static_cast<int16>(std::lround(v));
            }

            //! ワールド座標をパネル上のスクリーン座標へ射影する。カメラの後ろ (w <= 0) なら false を返す
            struct CursorScreenProjector
            {
                Matrix viewProjection; // 射影に使う view * projection
                float originX = 0.0f;      // パネル左上の X
                float originY = 0.0f;      // パネル左上の Y
                float width = 0.0f;        // パネル幅
                float height = 0.0f;       // パネル高さ

                bool operator()(const Vector3& world, ImVec2& out) const noexcept
                {
                    Vector2 pixel{};
                    float w = 0.0f;
                    if (!Gfx::TryProjectToPixels(viewProjection, world, width, height, pixel, w))
                    {
                        return false;
                    }
                    out.x = originX + pixel.x;
                    out.y = originY + pixel.y;
                    return true;
                }
            };
        } // namespace

        bool EditorMode::HasActorAtCell(int16 x, int16 y, int16 z) const noexcept
        {
            return m_findCellActor && m_findCellActor(x, y, z) != k_NoActorId;
        }

        bool EditorMode::IsCellOccupied(int16 x, int16 y, int16 z) const noexcept
        {
            return m_cellOccupied && m_cellOccupied(x, y, z);
        }

        bool EditorMode::IsSceneEditable() const noexcept
        {
            return !m_sceneEditable || m_sceneEditable();
        }

        void EditorMode::Tick(bool editShortcutsBlocked, const OS::InputPresses& presses) noexcept
        {
            if (!IsSceneEditable())
            {
                return;
            }

            UpdateCursorFromInput();
            HandlePlaceDeleteInput(presses);
            if (!editShortcutsBlocked)
            {
                HandleRotationInput(presses);
                HandleUndoRedoInput(presses);
                HandleSaveLoadInput(presses);
            }
            m_palette.TickInput(m_input, presses, editShortcutsBlocked);

            const Quaternion targetQuat = RotationToQuaternion(m_currentRotation);
            constexpr float k_RotationSpringRate = 12.0f;
            const float dt = OS::FrameTimer::FixedDelta();
            const float t = std::min(1.0f, k_RotationSpringRate * dt);
            m_displayedYawQuat = Quaternion::Slerp(m_displayedYawQuat, targetQuat, t);
        }

        void EditorMode::HandleSaveLoadInput(const OS::InputPresses& presses) noexcept
        {
            if (m_input == nullptr)
            {
                return;
            }

            const OS::Keyboard& kb = m_input->Keyboard();
            if (!kb.IsHeld(OS::Key::Ctrl))
            {
                return;
            }

            const bool shift = kb.IsHeld(OS::Key::Shift);
            if (presses.WasPressed(OS::Key::S))
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
            if (presses.WasPressed(OS::Key::O))
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
            // プレイ中の実体はプレイで動いた後の姿で、編集の内容ではないので書かない
            if (!IsSceneEditable() || !m_captureLevel)
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

            // 保存の出所は実体。捕捉関数で実体からシーンの JSON 文書を作って書く
            const nlohmann::json snapshot = m_captureLevel();
            const bool ok = SaveSceneToJsonFile(snapshot, *path);
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

        std::string EditorMode::QuitSaveLevelName() const noexcept
        {
            if (!m_currentLevelName.empty())
            {
                return m_currentLevelName;
            }

            // 起動レベルが実在するのに読めず seed で立ち上がった時は、元ファイルを潰さないよう退避名へ逃がす
            if (m_bootLevelLoadFailed)
            {
                return "Scenes/recovered_level";
            }

            // 通常起動 / 新規は同梱の起動シーンへ書き戻す
            return LevelNameOfScenePath(::Game::StartScenePath());
        }

        bool EditorMode::SaveForQuit() noexcept
        {
            const bool recovering = m_currentLevelName.empty() && m_bootLevelLoadFailed;
            const std::string name = QuitSaveLevelName();
            const bool ok = SaveLevelToName(name);
            if (recovering)
            {
                const char* prefix = "退避保存失敗: ";
                if (ok)
                {
                    prefix = "退避保存: ";
                }
                m_statusMessage = std::string(prefix) + name;
                m_statusError = !ok;
                m_statusTimer = k_StatusToastSeconds;
            }
            return ok;
        }

        bool EditorMode::OpenLevel(std::string_view name) noexcept
        {
            // プレイ中に組み直すと、編集へ戻る組み直しがプレイ開始の写しで上書きし、名前だけが新しいレベルを指す
            if (!IsSceneEditable())
            {
                return false;
            }
            const std::optional<std::string> path = BuildLevelPath(name);
            if (!path || !m_loadLevel)
            {
                return false;
            }

            nlohmann::json fresh;
            if (!LoadSceneFromJsonFile(fresh, *path))
            {
                // 起動レベルが実在するのに読めない時は、終了の保存が元のファイルを潰さないよう退避の名前へ逃がす
                if (m_currentLevelName.empty() && OS::FileSystem::Exists(*path))
                {
                    m_bootLevelLoadFailed = true;
                }
                return false;
            }

            // 新レベルなので Undo 履歴を捨てる。履歴が持つアクターは組み直しが捨てる管理クラスを指すので、
            // 組み直しより先に捨てる
            m_undo.Clear();
            m_loadLevel(std::move(fresh));
            m_savedUndoVersion = m_undo.Version();
            m_levelDirty = true;
            m_currentLevelName = QualifyLevelPath(SanitizeLevelPath(name));
            return true;
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
                if (!BuildLevelPath(result.targetName) || !m_loadLevel)
                {
                    m_fileBrowser.NotifyLoadResult(false, "不正な level name");
                    break;
                }

                if (OpenLevel(result.targetName))
                {
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
                m_statusTimer -= OS::FrameTimer::DeltaSeconds();
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

        void EditorMode::DrawCursorShapes(Gfx::DebugShapes& shapes) const noexcept
        {
            if (!IsSceneEditable() || !m_cursor.valid)
            {
                return;
            }

            const AABB placeBox(m_cursor.placementCenter,
                                    Vector3{k_CellHalfExtent, k_CellHalfExtent, k_CellHalfExtent});
            Color cursorColor = k_CursorOkColor;

            if (m_cursor.placementBlocked)
            {
                cursorColor = k_CursorBlockedColor;
            }

            shapes.AABB(placeBox, cursorColor);
        }

        void EditorMode::RenderCursorPreview() noexcept
        {
            if (!IsSceneEditable() || !m_cursor.valid)
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

            const Matrix vp = m_camera->ViewProjection();
            const Vector3 c = m_cursor.placementCenter;
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

            const Vector3 boxCorners[8] = {
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

            const float slopeAngle = m_palette.CurrentSlopeAngleDegrees();
            if (slopeAngle >= 0.0f)
            {
                const float angle = slopeAngle;
                const float rawHeight = std::tan(DegreesToRadians(angle)) * (2.0f * h);
                const float height = std::min(rawHeight, 2.0f * h);
                const float yBot = -h;
                const float yTop = -h + height;

                const Vector3 wedgeLocal[6] = {
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
                    const Vector3 r = Vector3::Transform(wedgeLocal[i], m_displayedYawQuat);
                    wedgeFront[i] = project(Vector3{c.x + r.x, c.y + r.y, c.z + r.z}, wedgeScreen[i]);
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

        std::vector<uint32> EditorMode::CreateActors(std::vector<nlohmann::json> objects)
        {
            // プレイ中に作った物は、編集へ戻る組み直しで消える。履歴だけがプレイの姿を指して残る
            std::vector<uint32> created;
            if (!IsSceneEditable() || m_applier == nullptr || !m_allocateId || objects.empty())
            {
                return created;
            }

            // 写した元の id から新しい id への表。一緒に作る物どうしの参照を付け替える
            std::unordered_map<uint32, uint32> idMap;
            created.reserve(objects.size());
            for (nlohmann::json& object : objects)
            {
                const uint32 sourceId = ActorJsonId(object);
                const uint32 id = m_allocateId();
                if (sourceId != k_NoActorId)
                {
                    idMap.emplace(sourceId, id);
                }
                SetActorJsonId(object, id);
                created.push_back(id);
            }

            std::vector<std::unique_ptr<ICommand>> commands;
            commands.reserve(objects.size());
            for (std::size_t i = 0; i < objects.size(); ++i)
            {
                RemapActorRefs(objects[i], idMap);
                commands.push_back(
                    std::make_unique<ActorSnapshotCommand>(created[i], std::nullopt, std::move(objects[i])));
            }
            m_undo.Push(MakeUndoUnit(std::move(commands)), *m_applier);
            m_levelDirty = true;
            return created;
        }

        void EditorMode::PlaceUnderCursorProgrammatic(int16 x, int16 y, int16 z) noexcept
        {
            if (!IsSceneEditable() || m_applier == nullptr || !m_findCellActor)
            {
                return;
            }

            const uint8 rotation = [this]() -> uint8 {
                if (m_palette.CurrentIsRotatable())
                {
                    return m_currentRotation;
                }
                return uint8{0};
            }();

            nlohmann::json placed = m_palette.CurrentTemplate();
            SetActorJsonPosition(placed,
                                  Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
            Editor::SetCellRotationStep(placed, rotation);

            const uint32 id = m_findCellActor(x, y, z);
            if (id == k_NoActorId)
            {
                // 地形の部品でない物は筆で置き換えない
                if (IsCellOccupied(x, y, z))
                {
                    return;
                }
                // 筆は置いた物を選ばない
                std::vector<nlohmann::json> objects;
                objects.push_back(std::move(placed));
                (void)CreateActors(std::move(objects));
                return;
            }

            // 今ある cell は同じ永続 id で置き換える。作るのでなく編集なので CreateActors を通さない
            std::optional<nlohmann::json> before = m_applier->CaptureActor(id);
            SetActorJsonId(placed, id);
            m_undo.Push(std::make_unique<Editor::ActorSnapshotCommand>(id, std::move(before), std::move(placed)),
                        *m_applier);
            m_levelDirty = true;
        }

        void EditorMode::DeleteAtProgrammatic(int16 x, int16 y, int16 z) noexcept
        {
            if (!IsSceneEditable() || m_applier == nullptr || !m_findCellActor)
            {
                return;
            }

            const uint32 id = m_findCellActor(x, y, z);
            if (id == k_NoActorId)
            {
                return;
            }
            std::optional<nlohmann::json> before = m_applier->CaptureActor(id);
            if (!before)
            {
                return;
            }
            m_undo.Push(std::make_unique<Editor::ActorSnapshotCommand>(id, std::move(before), std::nullopt),
                        *m_applier);
            m_levelDirty = true;
        }

        void EditorMode::RotateAtProgrammatic(int16 x, int16 y, int16 z) noexcept
        {
            if (!IsSceneEditable() || m_applier == nullptr || !m_findCellActor)
            {
                return;
            }

            const uint32 id = m_findCellActor(x, y, z);
            if (id == k_NoActorId)
            {
                return;
            }
            std::optional<nlohmann::json> before = m_applier->CaptureActor(id);
            if (!before)
            {
                return;
            }
            nlohmann::json after = *before;
            Editor::AddCellQuarterTurn(after);
            m_undo.Push(std::make_unique<Editor::ActorSnapshotCommand>(id, std::move(*before), std::move(after)),
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

            const Matrix vp = m_camera->ViewProjection();
            const Ray ray = Editor::ScreenToWorldRay(vp, ViewRectSize(view), localX, localY);

            float bestT = std::numeric_limits<float>::max();
            bool hit = false;
            CellCoord hitCell{};
            CellCoord normal{0, 1, 0};
            Vector3 hitPoint{};

            std::vector<CellCoord> cells;
            if (m_collectCells)
            {
                cells = m_collectCells();
            }
            for (const CellCoord& cell : cells)
            {
                const Vector3 center{
                    static_cast<float>(cell.x), static_cast<float>(cell.y), static_cast<float>(cell.z)};
                const AABB box(center, {k_CellHalfExtent, k_CellHalfExtent, k_CellHalfExtent});

                float t = 0.0f;
                if (ray.Intersects(box, t) && t < bestT)
                {
                    bestT = t;
                    hit = true;
                    hitCell = cell;
                    hitPoint = Vector3(ray.position.x + ray.direction.x * t,
                                           ray.position.y + ray.direction.y * t,
                                           ray.position.z + ray.direction.z * t);

                    const Vector3 d = hitPoint - center;
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
                const CellCoord place{static_cast<int16>(hitCell.x + normal.x),
                                      static_cast<int16>(hitCell.y + normal.y),
                                      static_cast<int16>(hitCell.z + normal.z)};

                m_cursor.valid = true;
                m_cursor.hit = hitCell;
                m_cursor.placementCenter =
                    Vector3{static_cast<float>(place.x), static_cast<float>(place.y), static_cast<float>(place.z)};
                m_cursor.place = place;
                m_cursor.placementBlocked = IsCellOccupied(place.x, place.y, place.z);
                return;
            }

            Vector3 cellCenter{};
            if (!Editor::TryGroundPlaneFallback(ray, cellCenter))
            {
                return;
            }

            m_cursor.valid = true;
            m_cursor.placementCenter = cellCenter;
            m_cursor.place = CellCoord{RoundToCell(cellCenter.x), RoundToCell(cellCenter.y), RoundToCell(cellCenter.z)};
            m_cursor.hit = m_cursor.place;
            m_cursor.placementBlocked = IsCellOccupied(m_cursor.place.x, m_cursor.place.y, m_cursor.place.z);
        }

        void EditorMode::HandlePlaceDeleteInput(const OS::InputPresses& presses) noexcept
        {
            // Game 窓の上では ImGui が常にマウスを要求するため、UI 上かどうかの判定は
            // カーソルがパネルの矩形内にあるかで判定する
            if (m_input == nullptr || !m_cursor.valid)
            {
                return;
            }
            if (m_inputSuppressed)
            {
                return;
            }

            if (presses.WasPressed(OS::MouseButton::Left) && !m_cursor.placementBlocked)
            {
                PlaceUnderCursorProgrammatic(m_cursor.place.x, m_cursor.place.y, m_cursor.place.z);
            }
            if (presses.WasPressed(OS::MouseButton::Right) &&
                HasActorAtCell(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z))
            {
                DeleteAtProgrammatic(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z);
            }

            OS::Gamepad& gp = m_input->Gamepad();
            if (!gp.IsConnected())
            {
                return;
            }

            if (presses.WasPressed(OS::GamepadButton::A) && !m_cursor.placementBlocked)
            {
                PlaceUnderCursorProgrammatic(m_cursor.place.x, m_cursor.place.y, m_cursor.place.z);
            }
            if (presses.WasPressed(OS::GamepadButton::B) &&
                HasActorAtCell(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z))
            {
                DeleteAtProgrammatic(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z);
            }
        }

        void EditorMode::HandleRotationInput(const OS::InputPresses& presses) noexcept
        {
            if (m_input == nullptr)
            {
                return;
            }
            if (m_inputSuppressed)
            {
                return;
            }

            const bool rotate =
                presses.WasPressed(OS::Key::R) ||
                (m_input->Gamepad().IsConnected() && presses.WasPressed(OS::GamepadButton::Y));

            if (!rotate || !m_cursor.valid)
            {
                return;
            }

            if (HasActorAtCell(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z))
            {
                RotateAtProgrammatic(m_cursor.hit.x, m_cursor.hit.y, m_cursor.hit.z);
            }
            else if (m_palette.CurrentIsRotatable())
            {
                m_currentRotation = static_cast<uint8>((m_currentRotation + 1) & 0x03);
            }
        }

        void EditorMode::HandleUndoRedoInput(const OS::InputPresses& presses) noexcept
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

            OS::Keyboard& kb = m_input->Keyboard();
            const bool ctrl = kb.IsHeld(OS::Key::Ctrl);
            const bool shift = kb.IsHeld(OS::Key::Shift);

            if (ctrl && shift && presses.WasPressed(OS::Key::Z))
            {
                PerformRedo();
                return;
            }
            if (ctrl && presses.WasPressed(OS::Key::Z))
            {
                PerformUndo();
            }
            if (ctrl && presses.WasPressed(OS::Key::Y))
            {
                PerformRedo();
            }
        }

        bool EditorMode::PerformUndo() noexcept
        {
            // 履歴は編集の姿を指す。プレイ中の実体へ当てると、プレイの姿に編集の姿が混ざる
            if (!IsSceneEditable() || m_applier == nullptr || !m_undo.Undo(*m_applier))
            {
                return false;
            }
            m_levelDirty = true;
            return true;
        }

        bool EditorMode::PerformRedo() noexcept
        {
            if (!IsSceneEditable() || m_applier == nullptr || !m_undo.Redo(*m_applier))
            {
                return false;
            }
            m_levelDirty = true;
            return true;
        }
    } // namespace Editor
} // namespace NS

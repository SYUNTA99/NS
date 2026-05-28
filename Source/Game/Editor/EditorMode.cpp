#include "Game/Editor/EditorMode.h"

#include "Framework/App/Application.h"
#include "Framework/Graphics/DebugDraw.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/CameraComponent.h"
#include "Framework/Scene/EditorCameraComponent.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Editor/AutoTile.h"
#include "Game/Editor/BlockRegistry.h"
#include "Game/Editor/LevelFilePaths.h"
#include "Game/Level/ChunkIO.h"
#include "Game/Level/LevelData.h"
#include "Game/Undo/DeleteCommand.h"
#include "Game/Undo/PlaceCommand.h"
#include "Game/Undo/RotateCommand.h"

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace NS::Game::Editor
{
    namespace
    {
        constexpr float kCellHalfExtent = 0.5f;

        const NS::Core::Color kCursorOkColor{0.1f, 1.0f, 0.1f, 1.0f};
        const NS::Core::Color kCursorBlockedColor{1.0f, 0.1f, 0.1f, 1.0f};

        [[nodiscard]] bool HasBlockAtCell(const NS::Game::Level::LevelData& level,
                                          std::int16_t x,
                                          std::int16_t y,
                                          std::int16_t z) noexcept
        {
            return std::any_of(level.blocks.begin(), level.blocks.end(), [x, y, z](const auto& b) {
                return b.x == x && b.y == y && b.z == z;
            });
        }

        [[nodiscard]] std::int16_t RoundToCell(float v) noexcept
        {
            return static_cast<std::int16_t>(std::lround(v));
        }
    } // namespace

    void EditorMode::Tick() noexcept
    {
        if (!m_active || m_level == nullptr)
            return;

        UpdateCursorFromInput();
        HandlePlaceDeleteInput();
        HandleRotationInput();
        HandleSpawnInput();
        HandleUndoRedoInput();
        HandleSaveLoadInput();
        m_palette.TickInput(m_input, m_imgui);
    }

    void EditorMode::HandleSaveLoadInput() noexcept
    {
        if (m_input == nullptr)
            return;
        const bool wantKb = (m_imgui != nullptr) && m_imgui->WantCaptureKeyboard();
        if (wantKb)
            return;

        const auto& kb = m_input->Keyboard();
        if (!kb.IsHeld(NS::Platform::Key::Ctrl))
            return;
        if (kb.IsPressed(NS::Platform::Key::S))
            m_fileBrowser.OpenSaveModal();
        if (kb.IsPressed(NS::Platform::Key::O))
            m_fileBrowser.OpenLoadModal();
    }

    void EditorMode::RenderFileBrowser() noexcept
    {
        if (m_level == nullptr)
            return;
        const auto result = m_fileBrowser.Render();
        switch (result.action)
        {
        case LevelFileBrowser::Action::RequestSave:
        {
            auto path = BuildLevelPath(result.targetName);
            if (!path)
            {
                m_fileBrowser.NotifySaveResult(false, "不正な level name");
                break;
            }
            // 失敗時は SaveLevelToFile 側でも write が失敗して NS_LOG_ERROR が出るので、 ここでは
            // 結果を保持せず本体の Save を試みる方が message を 1 本にまとめられる。
            (void)EnsureLevelsDirectoryExists();
            const bool ok = NS::Game::Level::SaveLevelToFile(*m_level, *path);
            m_fileBrowser.NotifySaveResult(ok, ok ? "保存成功" : "保存失敗");
            break;
        }
        case LevelFileBrowser::Action::RequestLoad:
        {
            auto path = BuildLevelPath(result.targetName);
            if (!path)
            {
                m_fileBrowser.NotifyLoadResult(false, "不正な level name");
                break;
            }
            NS::Game::Level::LevelData fresh;
            const bool ok = NS::Game::Level::LoadLevelFromFile(fresh, *path);
            if (ok)
            {
                // 新 level open で UndoStack 履歴は破棄 (古い level 用 Command が
                // 別 LevelData を pointer で持つため、 そのまま undo すると use-after-free 的 mismatch)。
                *m_level = std::move(fresh);
                m_undo.Clear();
                m_levelDirty = true;
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
    }

    void EditorMode::RenderCursorPreview() noexcept
    {
        if (!m_active || !m_cursor.valid)
            return;

        // DebugDraw への蓄積は維持 (将来 GPU 描画 path が整ったら自動的に表示される)。
        // 既存 test (CursorPreview.RendersAABBToDebugDraw) も buffered vertex を assert。
        const NS::Core::AABB placeBox(m_cursor.placementCenter,
                                      NS::Core::Vector3{kCellHalfExtent, kCellHalfExtent, kCellHalfExtent});
        NS::Graphics::DebugDraw::AABB(placeBox, m_cursor.placementBlocked ? kCursorBlockedColor : kCursorOkColor);

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
        // 即座に画面上で wireframe を確認できるよう、 ImGui の background DrawList に
        // 8 頂点を view-projection で screen 投影して 12 辺を線描画する。
        // DebugDraw::Flush の GPU 描画が未配線な間の代替手段。
        if (m_camera == nullptr)
            return;
        auto* app = NS::App::Application::Get();
        if (app == nullptr)
            return;
        const auto viewport = app->Window().Size();
        if (viewport.width <= 0 || viewport.height <= 0)
            return;

        const auto vp = m_camera->ViewProjection();
        const NS::Core::Vector3 c = m_cursor.placementCenter;
        constexpr float h = kCellHalfExtent;
        const NS::Core::Vector3 corners[8] = {
            {c.x - h, c.y - h, c.z - h},
            {c.x + h, c.y - h, c.z - h},
            {c.x + h, c.y + h, c.z - h},
            {c.x - h, c.y + h, c.z - h},
            {c.x - h, c.y - h, c.z + h},
            {c.x + h, c.y - h, c.z + h},
            {c.x + h, c.y + h, c.z + h},
            {c.x - h, c.y + h, c.z + h},
        };

        ImVec2 screen[8]{};
        bool inFront[8]{};
        for (int i = 0; i < 8; ++i)
        {
            const NS::Core::Vector4 worldH{corners[i].x, corners[i].y, corners[i].z, 1.0f};
            const NS::Core::Vector4 clip = NS::Core::Vector4::Transform(worldH, vp);
            if (clip.w <= 0.0f)
            {
                inFront[i] = false;
                continue;
            }
            const float ndcX = clip.x / clip.w;
            const float ndcY = clip.y / clip.w;
            screen[i].x = (ndcX * 0.5f + 0.5f) * static_cast<float>(viewport.width);
            screen[i].y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(viewport.height);
            inFront[i] = true;
        }

        static constexpr int kEdges[12][2] = {
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
        const ImU32 color = m_cursor.placementBlocked ? IM_COL32(255, 64, 64, 255) : IM_COL32(64, 255, 64, 255);
        if (ImDrawList* dl = ImGui::GetBackgroundDrawList())
        {
            for (const auto& e : kEdges)
            {
                if (inFront[e[0]] && inFront[e[1]])
                    dl->AddLine(screen[e[0]], screen[e[1]], color, 2.0f);
            }
        }
#endif
    }

    void EditorMode::PlaceUnderCursorProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_level == nullptr)
            return;
        m_undo.Push(
            std::make_unique<NS::Game::Undo::PlaceCommand>(x, y, z, m_palette.CurrentBlockId(), m_currentRotation),
            *m_level);
        m_levelDirty = true;
    }

    void EditorMode::DeleteAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_level == nullptr)
            return;
        m_undo.Push(std::make_unique<NS::Game::Undo::DeleteCommand>(x, y, z), *m_level);
        m_levelDirty = true;
    }

    void EditorMode::RotateAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_level == nullptr)
            return;
        m_undo.Push(std::make_unique<NS::Game::Undo::RotateCommand>(x, y, z, +1), *m_level);
        m_levelDirty = true;
    }

    void EditorMode::SetSpawnAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_level == nullptr)
            return;
        SetSpawnMarker(*m_level, x, y, z);
        m_levelDirty = true;
    }

    void EditorMode::UpdateCursorFromInput() noexcept
    {
        m_cursor = CursorState{};
        if (m_input == nullptr || m_camera == nullptr || m_level == nullptr)
            return;

        auto* app = NS::App::Application::Get();
        if (app == nullptr)
            return;
        const auto viewport = app->Window().Size();
        if (viewport.width <= 0 || viewport.height <= 0)
            return;

        const auto vp = m_camera->ViewProjection();
        const NS::Core::Ray ray =
            NS::Scene::EditorGridMath::ScreenToWorldRay(vp, viewport, m_input->Mouse().X(), m_input->Mouse().Y());

        float bestT = std::numeric_limits<float>::max();
        bool hit = false;
        std::int16_t hitX = 0;
        std::int16_t hitY = 0;
        std::int16_t hitZ = 0;
        NS::Core::Vector3 hitPoint{};
        NS::Core::Vector3 hitNormal{0.0f, 1.0f, 0.0f};

        for (const auto& b : m_level->blocks)
        {
            const NS::Core::Vector3 center{static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z)};
            const NS::Core::AABB box(center, {kCellHalfExtent, kCellHalfExtent, kCellHalfExtent});
            float t = 0.0f;
            if (ray.Intersects(box, t) && t < bestT)
            {
                bestT = t;
                hit = true;
                hitX = b.x;
                hitY = b.y;
                hitZ = b.z;
                hitPoint = NS::Core::Vector3(ray.position.x + ray.direction.x * t,
                                             ray.position.y + ray.direction.y * t,
                                             ray.position.z + ray.direction.z * t);

                // hit 面の法線は hitPoint - center で支配軸を見て決める
                const NS::Core::Vector3 d = hitPoint - center;
                const float ax = std::fabs(d.x);
                const float ay = std::fabs(d.y);
                const float az = std::fabs(d.z);
                if (ax > ay && ax > az)
                    hitNormal = NS::Core::Vector3{d.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
                else if (ay > az)
                    hitNormal = NS::Core::Vector3{0.0f, d.y > 0.0f ? 1.0f : -1.0f, 0.0f};
                else
                    hitNormal = NS::Core::Vector3{0.0f, 0.0f, d.z > 0.0f ? 1.0f : -1.0f};
            }
        }

        if (hit)
        {
            const auto placeCenter = NS::Scene::EditorGridMath::SnapHitToPlacementCell(hitPoint, hitNormal);
            m_cursor.valid = true;
            m_cursor.hitX = hitX;
            m_cursor.hitY = hitY;
            m_cursor.hitZ = hitZ;
            m_cursor.deleteCenter =
                NS::Core::Vector3{static_cast<float>(hitX), static_cast<float>(hitY), static_cast<float>(hitZ)};
            m_cursor.placementCenter = placeCenter;
            m_cursor.placeX = RoundToCell(placeCenter.x);
            m_cursor.placeY = RoundToCell(placeCenter.y);
            m_cursor.placeZ = RoundToCell(placeCenter.z);
            m_cursor.hitNormal = hitNormal;
            m_cursor.placementBlocked = HasBlockAtCell(*m_level, m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
            return;
        }

        NS::Core::Vector3 cellCenter{};
        if (!NS::Scene::EditorGridMath::TryGroundPlaneFallback(ray, cellCenter))
            return;

        m_cursor.valid = true;
        m_cursor.placementCenter = cellCenter;
        m_cursor.placeX = RoundToCell(cellCenter.x);
        m_cursor.placeY = RoundToCell(cellCenter.y);
        m_cursor.placeZ = RoundToCell(cellCenter.z);
        m_cursor.hitX = m_cursor.placeX;
        m_cursor.hitY = m_cursor.placeY;
        m_cursor.hitZ = m_cursor.placeZ;
        m_cursor.hitNormal = NS::Core::Vector3{0.0f, 1.0f, 0.0f};
        m_cursor.deleteCenter = cellCenter;
        m_cursor.placementBlocked = HasBlockAtCell(*m_level, m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
    }

    void EditorMode::HandlePlaceDeleteInput() noexcept
    {
        if (m_input == nullptr || m_level == nullptr || !m_cursor.valid)
            return;
        // ImGui UI が mouse を握っている時は editor の place / delete を発火しない (pitfall: UI クリックが
        // 裏で block を消す事故を防ぐ)
        if (m_imgui != nullptr && m_imgui->WantCaptureMouse())
            return;

        auto& mouse = m_input->Mouse();
        if (mouse.IsPressed(NS::Platform::MouseButton::Left) && !m_cursor.placementBlocked)
        {
            PlaceUnderCursorProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
        }
        if (mouse.IsPressed(NS::Platform::MouseButton::Right) &&
            HasBlockAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            DeleteAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
        }

        auto& gp = m_input->Gamepad(0);
        if (!gp.IsConnected())
            return;

        if (gp.IsPressed(NS::Platform::GamepadButton::A) && !m_cursor.placementBlocked)
            PlaceUnderCursorProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
        if (gp.IsPressed(NS::Platform::GamepadButton::B) &&
            HasBlockAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
            DeleteAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
    }

    void EditorMode::HandleRotationInput() noexcept
    {
        if (m_input == nullptr || m_level == nullptr || !m_cursor.valid)
            return;
        if (m_imgui != nullptr && m_imgui->WantCaptureKeyboard())
            return;

        const bool keyboardEdge = m_input->Keyboard().IsPressed(NS::Platform::Key::R);
        const bool gamepadEdge =
            m_input->Gamepad(0).IsConnected() && m_input->Gamepad(0).IsPressed(NS::Platform::GamepadButton::Y);
        if (!keyboardEdge && !gamepadEdge)
            return;

        if (HasBlockAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            // cursor 直下の既存 block を 90° 回転 (cell rotation)
            RotateAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
        }
        else
        {
            // 既存 block がなければ「次に置く block の rotation」 を進めるだけ (LevelData は変えない)
            m_currentRotation = static_cast<std::uint8_t>((m_currentRotation + 1) & 0x03);
        }
    }

    void EditorMode::HandleSpawnInput() noexcept
    {
        if (m_input == nullptr || m_level == nullptr || !m_cursor.valid)
            return;
        if (m_imgui != nullptr && m_imgui->WantCaptureKeyboard())
            return;

        if (m_input->Keyboard().IsPressed(NS::Platform::Key::G))
            SetSpawnAtProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
    }

    void EditorMode::HandleUndoRedoInput() noexcept
    {
        if (m_input == nullptr || m_level == nullptr)
            return;
        if (m_imgui != nullptr && m_imgui->WantCaptureKeyboard())
            return;

        auto& kb = m_input->Keyboard();
        const bool ctrl = kb.IsHeld(NS::Platform::Key::Ctrl);
        const bool shift = kb.IsHeld(NS::Platform::Key::Shift);

        // Ctrl+Shift+Z = Redo (Adobe / VS Code 流儀)、 Ctrl+Z = Undo、 Ctrl+Y = Redo
        if (ctrl && shift && kb.IsPressed(NS::Platform::Key::Z))
        {
            if (m_undo.Redo(*m_level))
                m_levelDirty = true;
            return;
        }
        if (ctrl && kb.IsPressed(NS::Platform::Key::Z))
        {
            if (m_undo.Undo(*m_level))
                m_levelDirty = true;
        }
        if (ctrl && kb.IsPressed(NS::Platform::Key::Y))
        {
            if (m_undo.Redo(*m_level))
                m_levelDirty = true;
        }
    }
} // namespace NS::Game::Editor

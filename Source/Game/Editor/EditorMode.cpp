#include "Game/Editor/EditorMode.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Clock.h"
#include "Framework/Graphics/DebugDraw.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/Components/CameraComponent.h"
#include "Framework/Scene/Components/EditorCameraComponent.h"
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

        const NS::Math::Color kCursorOkColor{0.1f, 1.0f, 0.1f, 1.0f};
        const NS::Math::Color kCursorBlockedColor{1.0f, 0.1f, 0.1f, 1.0f};

        [[nodiscard]] bool HasBlockAtCell(const NS::Game::Level::LevelData& level,
                                          std::int16_t x,
                                          std::int16_t y,
                                          std::int16_t z) noexcept
        {
            return NS::Game::Level::FindGridObjectAtCell(level, x, y, z) != NS::Game::Level::kNoObjectIndex;
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
        HandleUndoRedoInput();
        HandleSaveLoadInput();
        m_palette.TickInput(m_input, m_imgui);

        // 表示用 yaw quaternion を「現在の cursor rotation」 に Slerp で寄せて回転方向を視覚化する
        const auto targetQuat = NS::Math::Quaternion::CreateFromAxisAngle(
            {0.0f, 1.0f, 0.0f}, NS::Game::Editor::BlockRotationToYaw(m_currentRotation));
        constexpr float kRotationSpringRate = 12.0f;
        const float dt = NS::Core::FrameTimer::FixedDelta();
        const float t = std::min(1.0f, kRotationSpringRate * dt);
        m_displayedYawQuat = NS::Math::Quaternion::Slerp(m_displayedYawQuat, targetQuat, t);
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
            // 結果を保持せず本体の Save を試みる方が message を 1 本にまとめられる
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
                // 別 LevelData を pointer で持つため、 そのまま undo すると use-after-free 的 mismatch)
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

        // DebugDraw への蓄積は維持 (GPU 描画 path が整ったら自動表示される)
        const NS::Math::AABB placeBox(m_cursor.placementCenter,
                                      NS::Math::Vector3{kCellHalfExtent, kCellHalfExtent, kCellHalfExtent});
        NS::Graphics::DebugDraw::AABB(placeBox, m_cursor.placementBlocked ? kCursorBlockedColor : kCursorOkColor);

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
        // ImGui の background DrawList に 8 頂点を view-projection で screen 投影して 12 辺を線描画する
        if (m_camera == nullptr)
            return;
        auto* app = NS::App::Application::Get();
        if (app == nullptr)
            return;
        const auto viewport = app->Window().Size();
        if (viewport.width <= 0 || viewport.height <= 0)
            return;

        const auto vp = m_camera->ViewProjection();
        const NS::Math::Vector3 c = m_cursor.placementCenter;
        constexpr float h = kCellHalfExtent;
        const float vpW = static_cast<float>(viewport.width);
        const float vpH = static_cast<float>(viewport.height);

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (dl == nullptr)
            return;

        // world -> screen 投影。 clip.w<=0 (カメラ背後) は描画しない
        const auto project = [&](const NS::Math::Vector3& world, ImVec2& out) -> bool {
            const NS::Math::Vector4 worldH{world.x, world.y, world.z, 1.0f};
            const NS::Math::Vector4 clip = NS::Math::Vector4::Transform(worldH, vp);
            if (clip.w <= 0.0f)
                return false;
            out.x = ((clip.x / clip.w) * 0.5f + 0.5f) * vpW;
            out.y = (1.0f - ((clip.y / clip.w) * 0.5f + 0.5f)) * vpH;
            return true;
        };

        // セル枠の箱 (■) は軸そろえのまま固定。 向きは中の形状で示すので box 自体は回さない
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
            boxFront[i] = project(boxCorners[i], boxScreen[i]);

        static constexpr int kBoxEdges[12][2] = {
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
        const ImU32 boxColor = m_cursor.placementBlocked ? IM_COL32(255, 64, 64, 255) : IM_COL32(64, 255, 64, 255);
        for (const auto& e : kBoxEdges)
        {
            if (boxFront[e[0]] && boxFront[e[1]])
                dl->AddLine(boxScreen[e[0]], boxScreen[e[1]], boxColor, 2.0f);
        }

        // slope を選択中なら、 セル内に実形状の wedge を薄く描いて向きを可視化する
        // 斜面の稜線 (斜め) が m_displayedYawQuat で回るので、 回転が一目で分かる
        const std::uint16_t currentId = m_palette.CurrentBlockId();
        if (NS::Game::Editor::IsSlopeBlock(currentId))
        {
            constexpr float kPi = 3.14159265358979323846f;
            const float angle = NS::Game::Editor::GetSlopeAngleDegrees(currentId);
            const float rawHeight = std::tan(angle * (kPi / 180.0f)) * (2.0f * h);
            const float height = (rawHeight > 2.0f * h) ? 2.0f * h : rawHeight;
            const float yBot = -h;
            const float yTop = -h + height;

            // 6 頂点 (local、 +Z 側が高い斜面)。 BuildWedgeTriangles と同一規約
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

            // fBL=0 fBR=1 bBL=2 bBR=3 bTL=4 bTR=5。 0-4 / 1-5 が斜面の稜線 (斜め)
            static constexpr int kWedgeEdges[9][2] = {
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
            for (const auto& e : kWedgeEdges)
            {
                if (wedgeFront[e[0]] && wedgeFront[e[1]])
                    dl->AddLine(wedgeScreen[e[0]], wedgeScreen[e[1]], slopeColor, 1.0f);
            }
        }
#endif
    }

    void EditorMode::RenderSpawnMarker() noexcept
    {
        if (!m_active || m_level == nullptr)
            return;

        // カーソルが spawn セルに乗っている時は cursor preview と完全に重なるので、 描画を譲って
        // 黄色とそれ以外が滲む (アンチエイリアス境界 + 描画順依存) 問題を避ける
        if (m_cursor.valid && m_cursor.placeX == m_level->spawnX && m_cursor.placeY == m_level->spawnY &&
            m_cursor.placeZ == m_level->spawnZ)
            return;

        const NS::Math::Vector3 center{static_cast<float>(m_level->spawnX),
                                       static_cast<float>(m_level->spawnY),
                                       static_cast<float>(m_level->spawnZ)};
        const NS::Math::AABB marker(center, NS::Math::Vector3{kCellHalfExtent, kCellHalfExtent, kCellHalfExtent});
        const NS::Math::Color spawnColor{1.0f, 0.85f, 0.10f, 1.0f};
        NS::Graphics::DebugDraw::AABB(marker, spawnColor);

#if defined(NS_BUILD_DEBUG) || defined(NS_BUILD_DEV)
        if (m_camera == nullptr)
            return;
        auto* app = NS::App::Application::Get();
        if (app == nullptr)
            return;
        const auto viewport = app->Window().Size();
        if (viewport.width <= 0 || viewport.height <= 0)
            return;

        const auto vp = m_camera->ViewProjection();
        constexpr float h = kCellHalfExtent;
        const NS::Math::Vector3 corners[8] = {
            {center.x - h, center.y - h, center.z - h},
            {center.x + h, center.y - h, center.z - h},
            {center.x + h, center.y + h, center.z - h},
            {center.x - h, center.y + h, center.z - h},
            {center.x - h, center.y - h, center.z + h},
            {center.x + h, center.y - h, center.z + h},
            {center.x + h, center.y + h, center.z + h},
            {center.x - h, center.y + h, center.z + h},
        };

        ImVec2 screen[8]{};
        bool inFront[8]{};
        for (int i = 0; i < 8; ++i)
        {
            const NS::Math::Vector4 worldH{corners[i].x, corners[i].y, corners[i].z, 1.0f};
            const NS::Math::Vector4 clip = NS::Math::Vector4::Transform(worldH, vp);
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
        const ImU32 color = IM_COL32(255, 220, 0, 255);
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
        // 回転対象でない block (pole / water 等) は m_currentRotation が非ゼロでも 0 で焼き込む
        const std::uint16_t blockId = m_palette.CurrentBlockId();
        const std::uint8_t rotation = IsRotatableBlock(blockId) ? m_currentRotation : std::uint8_t{0};
        m_undo.Push(std::make_unique<NS::Game::Undo::PlaceCommand>(x, y, z, blockId, rotation), *m_level);
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
        // Object ツールモード中は grid 設置 cursor を出さない (カーソル追従の ■ プレビューがギズモ操作の邪魔になる)
        if (m_inputSuppressed)
            return;
        if (m_input == nullptr || m_camera == nullptr || m_level == nullptr)
            return;

        auto* app = NS::App::Application::Get();
        if (app == nullptr)
            return;
        const auto viewport = app->Window().Size();
        if (viewport.width <= 0 || viewport.height <= 0)
            return;

        const auto vp = m_camera->ViewProjection();
        const NS::Math::Ray ray =
            NS::Scene::EditorGridMath::ScreenToWorldRay(vp, viewport, m_input->Mouse().GetX(), m_input->Mouse().GetY());

        float bestT = std::numeric_limits<float>::max();
        bool hit = false;
        std::int16_t hitX = 0;
        std::int16_t hitY = 0;
        std::int16_t hitZ = 0;
        NS::Math::Vector3 hitPoint{};
        NS::Math::Vector3 hitNormal{0.0f, 1.0f, 0.0f};

        for (const auto& object : m_level->objects)
        {
            // grid カーソルの pick 対象は gridAligned のみ (自由配置物はギズモが拾う)
            if ((object.flags & NS::Game::Level::kObjectFlagGridAligned) == 0)
                continue;
            const std::int16_t cx = NS::Game::Level::ObjectCellX(object);
            const std::int16_t cy = NS::Game::Level::ObjectCellY(object);
            const std::int16_t cz = NS::Game::Level::ObjectCellZ(object);
            const NS::Math::Vector3 center{static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(cz)};
            const NS::Math::AABB box(center, {kCellHalfExtent, kCellHalfExtent, kCellHalfExtent});
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

                // hit 面の法線は hitPoint - center で支配軸を見て決める
                const NS::Math::Vector3 d = hitPoint - center;
                const float ax = std::fabs(d.x);
                const float ay = std::fabs(d.y);
                const float az = std::fabs(d.z);
                if (ax > ay && ax > az)
                    hitNormal = NS::Math::Vector3{d.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
                else if (ay > az)
                    hitNormal = NS::Math::Vector3{0.0f, d.y > 0.0f ? 1.0f : -1.0f, 0.0f};
                else
                    hitNormal = NS::Math::Vector3{0.0f, 0.0f, d.z > 0.0f ? 1.0f : -1.0f};
            }
        }

        if (hit)
        {
            // hitCell + 整数 normal で隣接セルを求める。 SnapHitToPlacementCell は境界座標を round するとさらに +1 され
            // 2 セル先に飛ぶ
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
            m_cursor.placementBlocked = HasBlockAtCell(*m_level, m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
            return;
        }

        NS::Math::Vector3 cellCenter{};
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
        m_cursor.hitNormal = NS::Math::Vector3{0.0f, 1.0f, 0.0f};
        m_cursor.deleteCenter = cellCenter;
        m_cursor.placementBlocked = HasBlockAtCell(*m_level, m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
    }

    void EditorMode::HandlePlaceDeleteInput() noexcept
    {
        if (m_input == nullptr || m_level == nullptr || !m_cursor.valid)
            return;
        // ImGui UI が mouse を握っている時は place / delete を発火しない (UI クリックが裏で block を消す事故を防ぐ)
        if (m_imgui != nullptr && m_imgui->WantCaptureMouse())
            return;
        // Object ツールモード中はギズモが LMB を専有するので grid の設置/削除は止める
        if (m_inputSuppressed)
            return;

        const std::uint16_t currentId = m_palette.CurrentBlockId();
        const bool spawnSlotActive = (currentId == kBlockIdSpawn);

        auto& mouse = m_input->Mouse();
        if (mouse.IsPressed(NS::Platform::MouseButton::Left))
        {
            if (spawnSlotActive)
            {
                // Spawn は世界に 1 点。 LevelData.spawnX/Y/Z を上書きするだけで BlockEntry は積まない
                SetSpawnAtProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
            }
            else if (!m_cursor.placementBlocked)
            {
                PlaceUnderCursorProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
            }
        }
        if (mouse.IsPressed(NS::Platform::MouseButton::Right) &&
            HasBlockAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            DeleteAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
        }

        auto& gp = m_input->Gamepad(0);
        if (!gp.IsConnected())
            return;

        if (gp.IsPressed(NS::Platform::GamepadButton::A))
        {
            if (spawnSlotActive)
                SetSpawnAtProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
            else if (!m_cursor.placementBlocked)
                PlaceUnderCursorProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
        }
        if (gp.IsPressed(NS::Platform::GamepadButton::B) &&
            HasBlockAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
            DeleteAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
    }

    void EditorMode::HandleRotationInput() noexcept
    {
        if (m_input == nullptr || m_level == nullptr)
            return;
        if (m_imgui != nullptr && m_imgui->WantCaptureKeyboard())
            return;
        // Object ツールモード中は R をギズモの Rotate ツールが使うので grid の 90° 回転は止める
        if (m_inputSuppressed)
            return;

        // R を 1 回叩くごとに 90° 回す。 slope も cube も 4 方向スナップ (押しっぱの連続回転はしない)
        const bool rotate =
            m_input->Keyboard().IsPressed(NS::Platform::Key::R) ||
            (m_input->Gamepad(0).IsConnected() && m_input->Gamepad(0).IsPressed(NS::Platform::GamepadButton::Y));
        if (!rotate || !m_cursor.valid)
            return;

        if (HasBlockAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            // cursor 直下の既存 block を 90° 回す。 回転対象外の block は無視する
            const std::size_t index =
                NS::Game::Level::FindGridObjectAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
            if (index != NS::Game::Level::kNoObjectIndex && IsRotatableBlock(m_level->objects[index].kind))
            {
                m_undo.Push(std::make_unique<NS::Game::Undo::RotateCommand>(
                                m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ, std::int8_t{1}),
                            *m_level);
                m_levelDirty = true;
            }
        }
        else if (IsRotatableBlock(m_palette.CurrentBlockId()))
        {
            // 既存 block がなければ次に置く block の向きを 90° 進める (4 方向で循環)
            m_currentRotation = static_cast<std::uint8_t>((m_currentRotation + 1) & 0x03);
        }
    }

    void EditorMode::HandleUndoRedoInput() noexcept
    {
        if (m_input == nullptr || m_level == nullptr)
            return;
        if (m_imgui != nullptr && m_imgui->WantCaptureKeyboard())
            return;
        // Object ツールモード中は Ctrl+Z をギズモの変形 undo が使うので grid の undo/redo は止める
        if (m_inputSuppressed)
            return;

        auto& kb = m_input->Keyboard();
        const bool ctrl = kb.IsHeld(NS::Platform::Key::Ctrl);
        const bool shift = kb.IsHeld(NS::Platform::Key::Shift);

        // Ctrl+Shift+Z = Redo、 Ctrl+Z = Undo、 Ctrl+Y = Redo
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

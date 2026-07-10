#include "Editor/EditorMode.h"

#include "Editor/GridMath.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/PlayerTuning.h"
#include "Editor/Undo/DeleteCommand.h"
#include "Editor/Undo/PlaceCommand.h"
#include "Editor/Undo/RotateCommand.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/LevelObjects.h"
#include "Game/Level/LevelIO.h"

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

#include <limits>

namespace NS::Editor
{
    namespace
    {
        constexpr float kCellHalfExtent = 0.5f;

        // cursor の回転値 0..3 を Y 軸 90° 刻みの yaw ラジアンへ写す
        constexpr float kQuarterTurnYaw = NS::Math::kPi * 0.5f;

        const NS::Math::Color kCursorOkColor{0.1f, 1.0f, 0.1f, 1.0f};
        const NS::Math::Color kCursorBlockedColor{1.0f, 0.1f, 0.1f, 1.0f};

        // 上書き保存などモーダル外通知を画面に出す秒数
        constexpr float kStatusToastSeconds = 2.5f;

        [[nodiscard]] bool HasObjectAtCell(const NS::Game::Level::LevelData& level,
                                           std::int16_t x,
                                           std::int16_t y,
                                           std::int16_t z) noexcept
        {
            return NS::Game::Level::FindObjectAtCell(level, x, y, z) != NS::Game::Level::kNoObjectIndex;
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
            {0.0f, 1.0f, 0.0f}, static_cast<float>(m_currentRotation) * kQuarterTurnYaw);
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
        const bool shift = kb.IsHeld(NS::Platform::Key::Shift);
        if (kb.IsPressed(NS::Platform::Key::S))
        {
            // Ctrl+S は現在レベルへ上書き、 Ctrl+Shift+S と未保存時は名前付け保存モーダル
            if (shift || m_currentLevelName.empty())
                m_fileBrowser.OpenSaveModal(m_currentLevelName);
            else
                OverwriteCurrentLevel();
        }
        if (kb.IsPressed(NS::Platform::Key::O))
            m_fileBrowser.OpenLoadModal();
    }

    bool EditorMode::SaveLevelToName(std::string_view name) noexcept
    {
        if (m_level == nullptr)
            return false;
        const auto safe = SanitizeLevelName(name);
        const auto path = BuildLevelPath(safe);
        if (safe.empty() || !path)
            return false;
        (void)EnsureLevelsDirectoryExists();
        const bool ok = NS::Game::Level::SaveLevelToFile(*m_level, *path);
        if (ok)
            m_currentLevelName = safe;
        return ok;
    }

    void EditorMode::OverwriteCurrentLevel() noexcept
    {
        const bool ok = SaveLevelToName(m_currentLevelName);
        const char* prefix = "保存失敗: ";
        if (ok)
            prefix = "上書き保存: ";
        m_statusMessage = prefix + m_currentLevelName;
        m_statusError = !ok;
        m_statusTimer = kStatusToastSeconds;
    }

    bool EditorMode::SaveForQuit() noexcept
    {
        // 現在名が無ければ起動時に読まれる new_level へ落として、 次回起動の表示と一致させる
        const std::string_view name = [this]() -> std::string_view {
            if (m_currentLevelName.empty())
                return std::string_view{"new_level"};
            return std::string_view{m_currentLevelName};
        }();
        return SaveLevelToName(name);
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
            // 保存 I/O は SaveLevelToName に集約する。 名前は browser 側で sanitize 済
            const bool ok = SaveLevelToName(result.targetName);
            const char* message = "保存失敗";
            if (ok)
                message = "保存成功";
            m_fileBrowser.NotifySaveResult(ok, message);
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
            NS::Game::Level::LevelLoadReport report{};
            const bool ok = NS::Game::Level::LoadLevelFromFile(fresh, *path, &report);
            if (ok)
            {
                // 旧形式から合成したプレイヤーには保存済みテンプレートの構成と値を写し、 移行前の手触りを保つ
                if (report.playerObjectCreated)
                {
                    const std::size_t playerIndex = NS::Game::Level::FindPlayerObjectIndex(fresh);
                    if (playerIndex != NS::Game::Level::kNoObjectIndex)
                        MergeSavedPlayerTuning(fresh.objects[playerIndex]);
                }
                // 新 level open で UndoStack 履歴は破棄する。 古い level 用 Command が
                // 別 LevelData を pointer で持つため、 そのまま undo すると use-after-free 的 mismatch
                *m_level = std::move(fresh);
                m_undo.Clear();
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

        // 上書き保存などモーダル外の保存結果を数秒だけ画面上部中央に出す。 入力は奪わない
#if NS_EDITOR_ENABLED
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
                constexpr ImGuiWindowFlags kFlags =
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;
                if (ImGui::Begin("##save_toast", nullptr, kFlags))
                {
                    const ImVec4 color = [this]() -> ImVec4 {
                        if (m_statusError)
                            return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                        return ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
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
            return;

        // DebugDraw への蓄積は維持し、 GPU 描画 path が整ったら自動表示される
        const NS::Math::AABB placeBox(m_cursor.placementCenter,
                                      NS::Math::Vector3{kCellHalfExtent, kCellHalfExtent, kCellHalfExtent});
        NS::Math::Color cursorColor = kCursorOkColor;
        if (m_cursor.placementBlocked)
            cursorColor = kCursorBlockedColor;
        NS::Graphics::DebugDraw::AABB(placeBox, cursorColor);

#if NS_EDITOR_ENABLED
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

        // clip.w<=0 はカメラ背後なので描画しない
        const auto project = [&](const NS::Math::Vector3& world, ImVec2& out) -> bool {
            const NS::Math::Vector4 worldH{world.x, world.y, world.z, 1.0f};
            const NS::Math::Vector4 clip = NS::Math::Vector4::Transform(worldH, vp);
            if (clip.w <= 0.0f)
                return false;
            out.x = ((clip.x / clip.w) * 0.5f + 0.5f) * vpW;
            out.y = (1.0f - ((clip.y / clip.w) * 0.5f + 0.5f)) * vpH;
            return true;
        };

        // セル枠の箱 ■ は軸そろえのまま固定。 向きは中の形状で示すので box 自体は回さない
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
        const ImU32 boxColor = [this]() -> ImU32 {
            if (m_cursor.placementBlocked)
                return IM_COL32(255, 64, 64, 255);
            return IM_COL32(64, 255, 64, 255);
        }();
        for (const auto& e : kBoxEdges)
        {
            if (boxFront[e[0]] && boxFront[e[1]])
                dl->AddLine(boxScreen[e[0]], boxScreen[e[1]], boxColor, 2.0f);
        }

        // slope を選択中なら、 セル内に実形状の wedge を薄く描いて向きを可視化する
        // 斜めの斜面の稜線が m_displayedYawQuat で回るので、 回転が一目で分かる
        const float slopeAngle = m_palette.CurrentSlopeAngleDegrees();
        if (slopeAngle >= 0.0f)
        {
            constexpr float kPi = 3.14159265358979323846f;
            const float angle = slopeAngle;
            const float rawHeight = std::tan(angle * (kPi / 180.0f)) * (2.0f * h);
            const float height = std::min(rawHeight, 2.0f * h);
            const float yBot = -h;
            const float yTop = -h + height;

            // local で +Z 側が高い斜面の 6 頂点。 BuildWedgeTriangles と同一規約
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

            // fBL=0 fBR=1 bBL=2 bBR=3 bTL=4 bTR=5。 0-4 / 1-5 が斜めの斜面の稜線
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

    void EditorMode::PlaceUnderCursorProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_level == nullptr)
            return;
        // 現在のブラシ = 複製元テンプレート。 配置は複製で行う
        const NS::Game::Level::ObjectInstance& tmpl = m_palette.CurrentTemplate();
        // water 等の回転対象でない block は m_currentRotation が非ゼロでも 0 で焼き込む
        const std::uint8_t rotation = [this]() -> std::uint8_t {
            if (m_palette.CurrentIsRotatable())
                return m_currentRotation;
            return std::uint8_t{0};
        }();
        m_undo.Push(std::make_unique<NS::Editor::PlaceCommand>(tmpl, x, y, z, rotation), *m_level);
        m_levelDirty = true;
    }

    void EditorMode::DeleteAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_level == nullptr)
            return;
        m_undo.Push(std::make_unique<NS::Editor::DeleteCommand>(x, y, z), *m_level);
        m_levelDirty = true;
    }

    void EditorMode::RotateAtProgrammatic(std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        if (m_level == nullptr)
            return;
        m_undo.Push(std::make_unique<NS::Editor::RotateCommand>(x, y, z, +1), *m_level);
        m_levelDirty = true;
    }

    void EditorMode::UpdateCursorFromInput() noexcept
    {
        m_cursor = CursorState{};
        // カーソル追従の ■ プレビューがギズモ操作の邪魔になるので Object ツールモード中は grid 設置 cursor を出さない
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
            NS::Editor::ScreenToWorldRay(vp, viewport, m_input->Mouse().GetX(), m_input->Mouse().GetY());

        float bestT = std::numeric_limits<float>::max();
        bool hit = false;
        std::int16_t hitX = 0;
        std::int16_t hitY = 0;
        std::int16_t hitZ = 0;
        NS::Math::Vector3 hitPoint{};
        NS::Math::Vector3 hitNormal{0.0f, 1.0f, 0.0f};

        for (const auto& object : m_level->objects)
        {
            // cursor の pick 対象は cell ブラシ配置物。 プレイヤーとカメラはギズモが拾うため除く
            if (!NS::Game::Level::IsCellBrushObject(object))
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
                {
                    float signX = -1.0f;
                    if (d.x > 0.0f)
                        signX = 1.0f;
                    hitNormal = NS::Math::Vector3{signX, 0.0f, 0.0f};
                }
                else if (ay > az)
                {
                    float signY = -1.0f;
                    if (d.y > 0.0f)
                        signY = 1.0f;
                    hitNormal = NS::Math::Vector3{0.0f, signY, 0.0f};
                }
                else
                {
                    float signZ = -1.0f;
                    if (d.z > 0.0f)
                        signZ = 1.0f;
                    hitNormal = NS::Math::Vector3{0.0f, 0.0f, signZ};
                }
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
            m_cursor.placementBlocked = HasObjectAtCell(*m_level, m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
            return;
        }

        NS::Math::Vector3 cellCenter{};
        if (!NS::Editor::TryGroundPlaneFallback(ray, cellCenter))
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
        m_cursor.placementBlocked = HasObjectAtCell(*m_level, m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
    }

    void EditorMode::HandlePlaceDeleteInput() noexcept
    {
        if (m_input == nullptr || m_level == nullptr || !m_cursor.valid)
            return;
        // UI クリックが裏で block を消す事故を防ぐため ImGui UI が mouse を握っている時は place / delete を発火しない
        if (m_imgui != nullptr && m_imgui->WantCaptureMouse())
            return;
        // Object ツールモード中はギズモが LMB を専有するので grid の設置/削除は止める
        if (m_inputSuppressed)
            return;

        auto& mouse = m_input->Mouse();
        if (mouse.IsPressed(NS::Platform::MouseButton::Left) && !m_cursor.placementBlocked)
        {
            PlaceUnderCursorProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
        }
        if (mouse.IsPressed(NS::Platform::MouseButton::Right) &&
            HasObjectAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            DeleteAtProgrammatic(m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
        }

        auto& gp = m_input->Gamepad(0);
        if (!gp.IsConnected())
            return;

        if (gp.IsPressed(NS::Platform::GamepadButton::A) && !m_cursor.placementBlocked)
            PlaceUnderCursorProgrammatic(m_cursor.placeX, m_cursor.placeY, m_cursor.placeZ);
        if (gp.IsPressed(NS::Platform::GamepadButton::B) &&
            HasObjectAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
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

        // R を 1 回叩くごとに 90° 回す。 slope も cube も 4 方向スナップで押しっぱの連続回転はしない
        const bool rotate =
            m_input->Keyboard().IsPressed(NS::Platform::Key::R) ||
            (m_input->Gamepad(0).IsConnected() && m_input->Gamepad(0).IsPressed(NS::Platform::GamepadButton::Y));
        if (!rotate || !m_cursor.valid)
            return;

        if (HasObjectAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ))
        {
            // cursor 直下の既存 block を 90° 回す。 回転対象外の block は無視する
            const std::size_t index =
                NS::Game::Level::FindObjectAtCell(*m_level, m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ);
            if (index != NS::Game::Level::kNoObjectIndex &&
                NS::Game::Blocks::IsRotatableObject(m_level->objects[index]))
            {
                m_undo.Push(std::make_unique<NS::Editor::RotateCommand>(
                                m_cursor.hitX, m_cursor.hitY, m_cursor.hitZ, std::int8_t{1}),
                            *m_level);
                m_levelDirty = true;
            }
        }
        else if (m_palette.CurrentIsRotatable())
        {
            // 既存 block がなければ次に置く block の向きを 90° 進めて 4 方向で循環させる
            m_currentRotation = static_cast<std::uint8_t>((m_currentRotation + 1) & 0x03);
        }
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
} // namespace NS::Editor

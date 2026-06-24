#pragma once

/// @file GizmoEditor.h
/// @brief NS::Editor::GizmoEditor — Object モードの選択 + Maya 風変形ギズモ
///
/// @details LevelData に属さない自由 Transform オブジェクトを Q/W/E/R の 4 ツールで
/// 選択・移動・回転・スケールする。変形軸は選択物の local 座標系に追従する (ハンドルの向き・
/// 移動方向・回転リング・スケール方向すべて選択物の回転で回す)。描画から独立して検証できるよう
/// view-projection 行列と viewport を Tick / Render に注入し、変形算出は静的純関数へ切り出す。undo は
/// grid 系の UndoStack とは別の TransformHistory で持ち、入力はツールモードで grid 系と排他にする
/// 依存: NS::Math, NS::Scene::Transform / GameObject, NS::Platform::Input / Key, NS::UI::ImGuiContext

#include "Framework/Math/Math.h"
#include "Framework/Platform/Keyboard.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace NS::Platform
{
    class Input;
}
namespace NS::UI
{
    class ImGuiContext;
}
namespace NS::Scene
{
    class Transform;
    class GameObject;
} // namespace NS::Scene

namespace NS::Editor
{
    /// 変形ツール種別 (Q=Select / W=Move / E=Rotate / R=Scale)
    enum class GizmoTool : std::uint8_t
    {
        Select,
        Move,
        Rotate,
        Scale
    };

    /// 変形の座標系 (X キーで切替)。Move / Rotate のみ従い、 Scale は常に Local 固定
    /// (非一様 world スケールは TRS で表現できないため)
    enum class GizmoSpace : std::uint8_t
    {
        Local,
        World
    };

    /// ギズモのハンドル軸。Uniform は scale 中心ハンドル (全軸均一)
    enum class GizmoAxis : std::uint8_t
    {
        None,
        X,
        Y,
        Z,
        Uniform
    };

    /// 1 オブジェクトの PRS スナップショット。undo の before / after に使う
    struct TransformState
    {
        NS::Math::Vector3 position{0.0f, 0.0f, 0.0f};
        NS::Math::Quaternion rotation{};
        NS::Math::Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    /// Object モードの選択 + 変形ギズモ本体
    class GizmoEditor
    {
    public:
        GizmoEditor() noexcept = default;
        ~GizmoEditor() noexcept = default;

        GizmoEditor(const GizmoEditor&) = delete;
        GizmoEditor& operator=(const GizmoEditor&) = delete;
        GizmoEditor(GizmoEditor&&) = delete;
        GizmoEditor& operator=(GizmoEditor&&) = delete;

        void SetInput(NS::Platform::Input* input) noexcept { m_input = input; }
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept { m_imgui = imgui; }

        /// 選択候補。objects と localHalfExtents は同一 index で対応する非所有 view
        void SetSelectableObjects(std::span<NS::Scene::GameObject* const> objects,
                                  std::span<const NS::Math::Vector3> localHalfExtents) noexcept;

        /// Object モード時のみ true。false の間は Tick / Render が何もしない
        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        /// 変形座標系 (Local / World)。Move / Rotate のみ従い Scale は常に Local
        void SetSpace(GizmoSpace space) noexcept { m_space = space; }
        [[nodiscard]] GizmoSpace Space() const noexcept { return m_space; }
        void ToggleSpace() noexcept
        {
            m_space = (m_space == GizmoSpace::Local) ? GizmoSpace::World : GizmoSpace::Local;
        }

        /// fixed step: ツール切替 → ピック → ドラッグ → 確定。vp / viewport は外部注入
        void Tick(const NS::Math::Matrix& viewProjection, NS::Math::Size2D viewport) noexcept;

        /// variable frame: 選択中ならギズモを ImGui drawlist へ積む
        void Render(const NS::Math::Matrix& viewProjection, NS::Math::Size2D viewport) noexcept;

        [[nodiscard]] GizmoTool Tool() const noexcept { return m_tool; }
        [[nodiscard]] NS::Scene::Transform* Selected() const noexcept { return m_selected; }
        /// 選択を外し、 進行中のドラッグも破棄する (モード切替で安全に呼べる)
        void ClearSelection() noexcept
        {
            m_selected = nullptr;
            m_dragging = false;
            m_dragAxis = GizmoAxis::None;
        }

        /// 選択対象を差し替える。 進行中のドラッグは破棄する (grid ブロック昇格後に新オブジェクトへ貼り直す)
        void SetSelected(NS::Scene::Transform* target) noexcept
        {
            m_selected = target;
            m_dragging = false;
            m_dragAxis = GizmoAxis::None;
        }

        /// ドラッグ中か。 controller が drag 開始 / 終了を検出して undo を確定するために使う
        [[nodiscard]] bool IsDragging() const noexcept { return m_dragging; }

        /// ray とローカル AABB (OBB 判定) で最近ヒットの index を返す。無ヒットは -1
        [[nodiscard]] static int PickNearestObb(const NS::Math::Ray& ray,
                                                std::span<const NS::Math::Matrix> worldMatrices,
                                                std::span<const NS::Math::Vector3> localHalfExtents) noexcept;

        /// 選択物の local 軸 (rotation で回した方向) を含む平面と ray の交点から、軸成分のみ反映した新 position を返す
        [[nodiscard]] static NS::Math::Vector3 ComputeAxisMove(const NS::Math::Vector3& startPos,
                                                               GizmoAxis axis,
                                                               const NS::Math::Quaternion& rotation,
                                                               const NS::Math::Ray& rayStart,
                                                               const NS::Math::Ray& rayNow,
                                                               bool snap) noexcept;

        /// 回転リングのドラッグを軸まわりの回転角 (rad) に変換する
        /// 軸は選択物の local 軸 (rotation で回した方向)。screenStart / screenEnd のカーソル ray を
        /// 軸直交平面に当て、 掴んだ点が運ばれた角を測る
        /// screen 2D 角と違いカメラがどちら側から見ても符号が反転せず、 平面を真横から見る縮退時は 0
        [[nodiscard]] static float WorldDragToAngle(const NS::Math::Vector3& origin,
                                                    GizmoAxis axis,
                                                    const NS::Math::Quaternion& rotation,
                                                    const NS::Math::Matrix& viewProjection,
                                                    NS::Math::Size2D viewport,
                                                    NS::Math::Vector2 screenStart,
                                                    NS::Math::Vector2 screenEnd) noexcept;

        /// startRot を axis 周りに angleRad 回した新 rotation を返す
        /// worldSpace=false は選択物の local 軸、true は world 軸で回す (合成基準は常に startRot)
        [[nodiscard]] static NS::Math::Quaternion ComputeAxisRotate(const NS::Math::Quaternion& startRot,
                                                                    GizmoAxis axis,
                                                                    float angleRad,
                                                                    bool snap,
                                                                    bool worldSpace = false) noexcept;

        /// 軸方向の screen ドラッグ量をスケール変化量に変換する
        [[nodiscard]] static float ScreenDragToScaleAmount(NS::Math::Vector2 axisDir2d,
                                                           NS::Math::Vector2 dragPixels) noexcept;

        /// axis (Uniform 含む) と amount から新 scale を返す。0 以下は最小正値に clamp
        [[nodiscard]] static NS::Math::Vector3 ComputeScale(const NS::Math::Vector3& startScale,
                                                            GizmoAxis axis,
                                                            float amount,
                                                            bool snap) noexcept;

        /// Q/W/E/R をツールに対応付ける。対象外キーは current を素通しする
        [[nodiscard]] static GizmoTool ToolForKey(GizmoTool current, NS::Platform::Key key) noexcept;

        /// gizmoOrigin と 3 軸端点 (rotation で回した local 軸) を screen 投影し、mouse2d に最も近い軸ハンドルを返す
        /// Select は常に None、Scale は中心 Uniform ハンドルを優先、閾値外/不正 viewport は None
        [[nodiscard]] static GizmoAxis ToolHandlePick(const NS::Math::Vector3& gizmoOrigin,
                                                      const NS::Math::Quaternion& rotation,
                                                      GizmoTool tool,
                                                      NS::Math::Vector2 mouse2d,
                                                      const NS::Math::Matrix& viewProjection,
                                                      NS::Math::Size2D viewport) noexcept;

        /// テスト用。Tick を介さずツール状態を注入する
        void SetToolForTest(GizmoTool tool) noexcept { m_tool = tool; }
        /// テスト用。選択を直接注入する
        void SelectForTest(NS::Scene::Transform* target) noexcept { m_selected = target; }
        /// テスト用。screen 上のドラッグを 1 操作分 live Transform へ適用する
        void ApplyDragForTest(const NS::Math::Matrix& viewProjection,
                              NS::Math::Size2D viewport,
                              GizmoAxis axis,
                              NS::Math::Vector2 screenStart,
                              NS::Math::Vector2 screenEnd) noexcept;

    private:
        void OnToolKey(NS::Platform::Key key) noexcept;

        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;
        std::span<NS::Scene::GameObject* const> m_objects{};
        std::span<const NS::Math::Vector3> m_halfExtents{};
        bool m_active = false;
        GizmoTool m_tool = GizmoTool::Select;
        GizmoSpace m_space = GizmoSpace::Local;
        NS::Scene::Transform* m_selected = nullptr;

        bool m_dragging = false;
        GizmoAxis m_dragAxis = GizmoAxis::None;
        NS::Math::Vector2 m_dragStartScreen{};
        TransformState m_dragBefore{};
    };
} // namespace NS::Editor

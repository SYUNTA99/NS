#pragma once

#include "Editor/GridMath.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Platform/Keyboard.h"

#include <span>

namespace NS::Platform
{
    class Input;
}
namespace NS::UI
{
    class ImGuiContext;
}
namespace NS::Object
{
    class Transform;
    class GameObject;
} // namespace NS::Object

namespace NS::Editor
{
    //! @brief 変形操作のツール種別
    enum class GizmoTool : std::uint8_t
    {
        Select,
        Move,
        Rotate,
        Scale
    };

    //! @brief 変形操作の基準となる座標系
    //! @note スケール操作は常にLocal空間で適用される
    enum class GizmoSpace : std::uint8_t
    {
        Local,
        World
    };

    //! @brief ギズモ操作の対象となる軸
    enum class GizmoAxis : std::uint8_t
    {
        None,
        X,
        Y,
        Z,
        Uniform
    };

    //! @brief 位置・回転・スケールのスナップショット
    struct TransformState
    {
        NS::Core::Vector3 position{0.0f, 0.0f, 0.0f};
        NS::Core::Quaternion rotation{};
        NS::Core::Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    //! @brief オブジェクトの選択と、変形ギズモのドラッグによる Transform 書き換え
    class GizmoEditor : public NS::Core::NonCopyable
    {
    public:
        GizmoEditor() noexcept = default;
        ~GizmoEditor() noexcept = default;

        void SetInput(NS::Platform::Input* input) noexcept { m_input = input; }
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept { m_imgui = imgui; }

        //! @brief 選択できる配置物と、そのローカル境界サイズを設定する
        //! @param[in] objects 選択対象となるオブジェクト
        //! @param[in] localHalfExtents 各オブジェクトに対応するローカル境界サイズ
        //! @param[in] pickable 選択の優先度を下げるマスク。省略できる
        void SetSelectableObjects(std::span<NS::Object::GameObject* const> objects,
                                  std::span<const NS::Core::Vector3> localHalfExtents,
                                  std::span<const std::uint8_t> pickable = {}) noexcept;

        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        void SetSpace(GizmoSpace space) noexcept { m_space = space; }

        //! ギズモの座標系を Local / World で切り替える
        void ToggleSpace() noexcept
        {
            m_space = (m_space == GizmoSpace::Local) ? GizmoSpace::World : GizmoSpace::Local;
        }

        //! マウスがゲーム表示パネル上に居るかを渡す。偽の間は選択クリックとハンドル掴みを受けない
        void SetViewHovered(bool hovered) noexcept { m_viewHovered = hovered; }

        //! @brief 毎フレーム入力を見て、選択とドラッグによる変形を進める
        //! @param[in] view ゲーム表示パネルの矩形。マウスはこの矩形基準のローカル座標で扱う
        void Tick(const NS::Core::Matrix& viewProjection, const ViewRect& view) noexcept;

        //! 現在の選択対象に対するギズモのUI描画コマンドを発行する。パネル外はクリップされる
        void Render(const NS::Core::Matrix& viewProjection, const ViewRect& view) noexcept;

        [[nodiscard]] GizmoTool Tool() const noexcept { return m_tool; }
        [[nodiscard]] NS::Object::Transform* Selected() const noexcept { return m_selected; }

        //! @brief 選択を解除し、進行中のドラッグを取り消す
        void ClearSelection() noexcept
        {
            m_selected = nullptr;
            m_dragging = false;
            m_dragAxis = GizmoAxis::None;
        }

        //! @brief 選択対象を直接指定して変更する。進行中のドラッグ操作はキャンセルされる
        void SetSelected(NS::Object::Transform* target) noexcept
        {
            m_selected = target;
            m_dragging = false;
            m_dragAxis = GizmoAxis::None;
        }

        //! ギズモのハンドルをドラッグして操作中かどうかを返す
        [[nodiscard]] bool IsDragging() const noexcept { return m_dragging; }

        //! @brief 視線レイを配置物の OBB へ当て、最も手前の添字を返す
        //! @return ヒットした場合はそのインデックス、ヒットしなかった場合は -1
        [[nodiscard]] static int PickNearestOBB(const NS::Core::Ray& ray,
                                                std::span<const NS::Core::Matrix> worldMatrices,
                                                std::span<const NS::Core::Vector3> localHalfExtents,
                                                std::span<const std::uint8_t> pickMask = {}) noexcept;

        //! 指定されたギズモ軸に沿った移動後の新しいワールド座標を計算する
        [[nodiscard]] static NS::Core::Vector3 ComputeAxisMove(const NS::Core::Vector3& startPos,
                                                               GizmoAxis axis,
                                                               const NS::Core::Quaternion& rotation,
                                                               const NS::Core::Ray& rayStart,
                                                               const NS::Core::Ray& rayNow,
                                                               bool snap) noexcept;

        //! 画面のドラッグ量を、指定軸まわりの回転角 (ラジアン) へ変換する
        [[nodiscard]] static float WorldDragToAngle(const NS::Core::Vector3& origin,
                                                    GizmoAxis axis,
                                                    const NS::Core::Quaternion& rotation,
                                                    const NS::Core::Matrix& viewProjection,
                                                    NS::Core::Size2D viewport,
                                                    NS::Core::Vector2 screenStart,
                                                    NS::Core::Vector2 screenEnd) noexcept;

        //! 指定の軸と角度から新しい回転を計算する
        [[nodiscard]] static NS::Core::Quaternion ComputeAxisRotate(const NS::Core::Quaternion& startRot,
                                                                    GizmoAxis axis,
                                                                    float angleRad,
                                                                    bool snap,
                                                                    bool worldSpace = false) noexcept;

        //! スクリーンのドラッグ量を、スケールへ足し引きする変化量に変換する
        [[nodiscard]] static float ScreenDragToScaleAmount(NS::Core::Vector2 axisDir2d,
                                                           NS::Core::Vector2 dragPixels) noexcept;

        //! 指定された軸とスケール変化量に基づく、新しいスケールベクトルを計算する
        [[nodiscard]] static NS::Core::Vector3 ComputeScale(const NS::Core::Vector3& startScale,
                                                            GizmoAxis axis,
                                                            float amount,
                                                            bool snap) noexcept;

        //! 入力されたキーに応じたギズモツール種別を返す
        [[nodiscard]] static GizmoTool ToolForKey(GizmoTool current, NS::Platform::Key key) noexcept;

        //! @brief マウス座標から、クリックされたギズモのハンドルを判定して返す
        [[nodiscard]] static GizmoAxis ToolHandlePick(const NS::Core::Vector3& gizmoOrigin,
                                                      const NS::Core::Quaternion& rotation,
                                                      GizmoTool tool,
                                                      NS::Core::Vector2 mouse2d,
                                                      const NS::Core::Matrix& viewProjection,
                                                      NS::Core::Size2D viewport) noexcept;

        void SetToolForTest(GizmoTool tool) noexcept { m_tool = tool; }
        void SelectForTest(NS::Object::Transform* target) noexcept { m_selected = target; }
        void ApplyDragForTest(const NS::Core::Matrix& viewProjection,
                              NS::Core::Size2D viewport,
                              GizmoAxis axis,
                              NS::Core::Vector2 screenStart,
                              NS::Core::Vector2 screenEnd) noexcept;

    private:
        void OnToolKey(NS::Platform::Key key) noexcept;

        NS::Platform::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;

        std::span<NS::Object::GameObject* const> m_objects{}; //!< 選択判定の対象となるオブジェクト
        std::span<const NS::Core::Vector3> m_halfExtents{};   //!< 各オブジェクトのローカル境界サイズ
        std::span<const std::uint8_t> m_pickable{};           //!< 選択の有効状態や優先度を示すマスク

        bool m_active = false;                       //!< ギズモ操作が有効かどうか
        bool m_viewHovered = true;                   //!< マウスがパネル上に居るか。全画面時は常に真
        GizmoTool m_tool = GizmoTool::Move;          //!< 現在の変形ツール
        GizmoSpace m_space = GizmoSpace::Local;      //!< 変形の座標系
        NS::Object::Transform* m_selected = nullptr; //!< 選択中の Transform

        bool m_dragging = false;                //!< ドラッグ中か
        GizmoAxis m_dragAxis = GizmoAxis::None; //!< ドラッグ中の軸
        NS::Core::Vector2 m_dragStartScreen{};  //!< ドラッグ開始時のスクリーン座標
        TransformState m_dragBefore{};          //!< ドラッグ開始時の 位置・回転・スケール
    };
} // namespace NS::Editor

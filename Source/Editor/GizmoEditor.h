#pragma once

#include "Editor/GridMath.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Windows/Keyboard.h"

#include <span>

namespace NS::OS
{
    class Input;
}
namespace NS::UI
{
    class ImGuiContext;
}
namespace NS::Obj
{
    class Transform;
    class Actor;
} // namespace NS::Obj

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
        NS::Vector3 position{0.0f, 0.0f, 0.0f};
        NS::Quaternion rotation{};
        NS::Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    //! @brief オブジェクトの選択と、変形ギズモのドラッグによる Transform 書き換え
    class GizmoEditor : public NS::NonCopyable
    {
    public:
        void SetInput(NS::OS::Input* input) noexcept { m_input = input; }
        void SetImGui(NS::UI::ImGuiContext* imgui) noexcept { m_imgui = imgui; }

        //! @brief 選択できる配置物を設定する
        //! @param[in] objects 選択対象となるオブジェクト
        //! @param[in] pickable objects と同じ添字の優先度。0 の配置物は 1 の配置物に当たらなかった時だけ選ぶ。
        //! 省略すると全部を同じに扱う
        void SetSelectableObjects(std::span<NS::Obj::Actor* const> objects,
                                  std::span<const std::uint8_t> pickable = {}) noexcept;

        void SetActive(bool active) noexcept { m_active = active; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }

        void SetSpace(GizmoSpace space) noexcept { m_space = space; }

        //! ギズモの座標系を Local / World で切り替える
        void ToggleSpace() noexcept
        {
            if (m_space == GizmoSpace::Local)
            {
                m_space = GizmoSpace::World;
            }
            else
            {
                m_space = GizmoSpace::Local;
            }
        }

        //! @brief 毎フレーム入力を見て、選択とドラッグによる変形を進める
        //! @param[in] view ゲーム表示パネルの矩形。マウスはこの矩形基準のローカル座標で扱う
        //! @param[in] viewHovered マウスがそのパネル上に居るか。偽の間は選択クリックとハンドル掴みを受けない
        void Tick(const NS::Matrix& viewProjection, const ViewRect& view, bool viewHovered) noexcept;

        //! 現在の選択対象に対するギズモのUI描画コマンドを発行する。パネル外はクリップされる
        void Render(const NS::Matrix& viewProjection, const ViewRect& view) noexcept;

        [[nodiscard]] GizmoTool Tool() const noexcept { return m_tool; }
        [[nodiscard]] NS::Obj::Transform* Selected() const noexcept { return m_selected; }

        //! @brief 選択を解除し、進行中のドラッグを取り消す
        void ClearSelection() noexcept { SetSelected(nullptr); }

        //! @brief 選択対象を直接指定して変更する。進行中のドラッグ操作はキャンセルされる
        void SetSelected(NS::Obj::Transform* target) noexcept
        {
            m_selected = target;
            m_dragging = false;
            m_dragAxis = GizmoAxis::None;
        }

        //! ギズモのハンドルをドラッグして操作中かどうかを返す
        [[nodiscard]] bool IsDragging() const noexcept { return m_dragging; }

        //! @brief 視線レイを配置物の OBB へ当て、最も手前の添字を返す
        //! @param[in] localBounds worldMatrices と同じ添字の判定箱。行列のローカル空間で、中心は原点でなくてよい
        //! @param[in] pickMask 0 の添字を飛ばす。空なら全部を見る
        //! @return ヒットした場合はそのインデックス、ヒットしなかった場合は -1
        [[nodiscard]] static int PickNearestOBB(const NS::Ray& ray,
                                                std::span<const NS::Matrix> worldMatrices,
                                                std::span<const NS::AABB> localBounds,
                                                std::span<const std::uint8_t> pickMask = {}) noexcept;

        //! 指定されたギズモ軸に沿った移動後の新しいワールド座標を計算する
        [[nodiscard]] static NS::Vector3 ComputeAxisMove(const NS::Vector3& startPos,
                                                         GizmoAxis axis,
                                                         const NS::Quaternion& rotation,
                                                         const NS::Ray& rayStart,
                                                         const NS::Ray& rayNow,
                                                         bool snap) noexcept;

        //! 画面のドラッグ量を、指定軸まわりの回転角 (ラジアン) へ変換する
        [[nodiscard]] static float WorldDragToAngle(const NS::Vector3& origin,
                                                    GizmoAxis axis,
                                                    const NS::Quaternion& rotation,
                                                    const NS::Matrix& viewProjection,
                                                    NS::Size2D viewport,
                                                    NS::Vector2 screenStart,
                                                    NS::Vector2 screenEnd) noexcept;

        //! 指定の軸と角度から新しい回転を計算する
        [[nodiscard]] static NS::Quaternion ComputeAxisRotate(const NS::Quaternion& startRot,
                                                              GizmoAxis axis,
                                                              float angleRad,
                                                              bool snap,
                                                              bool worldSpace = false) noexcept;

        //! スクリーンのドラッグ量を、スケールへ足し引きする変化量に変換する
        [[nodiscard]] static float ScreenDragToScaleAmount(NS::Vector2 axisDir2d, NS::Vector2 dragPixels) noexcept;

        //! 指定された軸とスケール変化量に基づく、新しいスケールベクトルを計算する
        [[nodiscard]] static NS::Vector3 ComputeScale(const NS::Vector3& startScale,
                                                      GizmoAxis axis,
                                                      float amount,
                                                      bool snap) noexcept;

        //! 入力されたキーに応じたギズモツール種別を返す
        [[nodiscard]] static GizmoTool ToolForKey(GizmoTool current, NS::OS::Key key) noexcept;

        //! @brief マウス座標から、クリックされたギズモのハンドルを判定して返す
        [[nodiscard]] static GizmoAxis ToolHandlePick(const NS::Vector3& gizmoOrigin,
                                                      const NS::Quaternion& rotation,
                                                      GizmoTool tool,
                                                      NS::Vector2 mouse2d,
                                                      const NS::Matrix& viewProjection,
                                                      NS::Size2D viewport) noexcept;

    private:
        NS::OS::Input* m_input = nullptr;
        NS::UI::ImGuiContext* m_imgui = nullptr;

        std::span<NS::Obj::Actor* const> m_objects{}; //!< 選択判定の対象となるオブジェクト
        std::span<const std::uint8_t> m_pickable{};   //!< 選択の優先度。1 の配置物を先に選ぶ

        bool m_active = false;                    //!< ギズモ操作が有効かどうか
        GizmoTool m_tool = GizmoTool::Move;       //!< 現在の変形ツール
        GizmoSpace m_space = GizmoSpace::Local;   //!< 変形の座標系
        NS::Obj::Transform* m_selected = nullptr; //!< 選択中の Transform

        bool m_dragging = false;                //!< ドラッグ中か
        GizmoAxis m_dragAxis = GizmoAxis::None; //!< ドラッグ中の軸
        NS::Vector2 m_dragStartScreen{};        //!< ドラッグ開始時のスクリーン座標
        TransformState m_dragBefore{};          //!< ドラッグ開始時の 位置・回転・スケール
    };
} // namespace NS::Editor

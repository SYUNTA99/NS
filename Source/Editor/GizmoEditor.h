#pragma once

#include "Editor/GridMath.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/Keyboard.h"

#include <span>

namespace NS
{
    namespace OS
    {
        class Input;
        class InputPresses;
    } // namespace OS
} // namespace NS
namespace NS
{
    class Transform;
    class SceneActor;
} // namespace NS

namespace NS
{
    namespace Editor
    {
        //! @brief 変形操作のツール種別
        enum class GizmoTool : uint8
        {
            Select,
            Move,
            Rotate,
            Scale
        };

        //! @brief 変形操作の基準となる座標系
        //! @note スケール操作は常にLocal空間で適用される
        enum class GizmoSpace : uint8
        {
            Local,
            World
        };

        //! @brief ギズモ操作の対象となる軸
        enum class GizmoAxis : uint8
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
            Vector3 position{0.0f, 0.0f, 0.0f};
            Quaternion rotation{};
            Vector3 scale{1.0f, 1.0f, 1.0f};
        };

        //! @brief オブジェクトの選択と、変形ギズモのドラッグによる Transform 書き換え
        class GizmoEditor : public NonCopyable
        {
        public:
            void SetInput(OS::Input* input) noexcept { m_input = input; }

            //! 選択候補を非所有で参照する。配列の格納元は使用中、有効に保つ
            //! pickable は objects と同じ添字。1 に当たらない場合だけ 0 を調べ、省略時は同じ優先度
            void SetSelectableActors(std::span<SceneActor* const> objects,
                                      std::span<const uint8> pickable = {}) noexcept;

            //! @brief ギズモ操作の有効を切り替える。休ませると進行中のドラッグを取り消す
            void SetActive(bool active) noexcept
            {
                m_active = active;
                if (!active)
                {
                    m_dragging = false;
                    m_dragAxis = GizmoAxis::None;
                }
            }
            [[nodiscard]] bool IsActive() const noexcept { return m_active; }

            void SetSpace(GizmoSpace space) noexcept { m_space = space; }

            //! ギズモを局所座標系と世界座標系で切り替える
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
            //! @param[in] editShortcutsBlocked ResolveInputOwnership の答え。
            //! 真の間は道具と座標系を切り替えるキーを受けない
            //! @param[in] presses 持ち主がこの固定ステップに読んだ押し
            void Tick(const Matrix& viewProjection,
                      const ViewRect& view,
                      bool viewHovered,
                      bool editShortcutsBlocked,
                      const OS::InputPresses& presses) noexcept;

            //! 現在の選択対象に対するギズモのUI描画コマンドを発行する。パネル外はクリップされる
            void Render(const Matrix& viewProjection, const ViewRect& view) noexcept;

            [[nodiscard]] GizmoTool Tool() const noexcept { return m_tool; }
            [[nodiscard]] Transform* Selected() const noexcept { return m_selected; }

            //! @brief 選択を解除し、進行中のドラッグを取り消す
            void ClearSelection() noexcept { SetSelected(nullptr); }

            //! @brief 選択対象を直接指定して変更する。進行中のドラッグ操作はキャンセルされる
            void SetSelected(Transform* target) noexcept
            {
                m_selected = target;
                m_dragging = false;
                m_dragAxis = GizmoAxis::None;
            }

            [[nodiscard]] bool IsDragging() const noexcept { return m_dragging; }

            //! @brief 視線レイをアクターの OBB へ当て、最も手前の添字を返す
            //! @param[in] localBounds worldMatrices と同じ添字の判定箱。行列のローカル空間で、中心は原点でなくてよい
            //! @param[in] pickMask 0 の添字を飛ばす。空なら全部を見る
            //! @return ヒットした場合はそのインデックス、ヒットしなかった場合は -1
            [[nodiscard]] static int PickNearestOBB(const Ray& ray,
                                                    std::span<const Matrix> worldMatrices,
                                                    std::span<const AABB> localBounds,
                                                    std::span<const uint8> pickMask = {}) noexcept;

            //! 指定されたギズモ軸に沿った移動後の新しいワールド座標を計算する
            [[nodiscard]] static Vector3 ComputeAxisMove(const Vector3& startPos,
                                                             GizmoAxis axis,
                                                             const Quaternion& rotation,
                                                             const Ray& rayStart,
                                                             const Ray& rayNow,
                                                             bool snap) noexcept;

            //! 画面のドラッグ量を、指定軸まわりの回転角 (ラジアン) へ変換する
            [[nodiscard]] static float WorldDragToAngle(const Vector3& origin,
                                                        GizmoAxis axis,
                                                        const Quaternion& rotation,
                                                        const Matrix& viewProjection,
                                                        Size2D viewport,
                                                        Vector2 screenStart,
                                                        Vector2 screenEnd) noexcept;

            //! 指定の軸と角度から新しい回転を計算する
            [[nodiscard]] static Quaternion ComputeAxisRotate(const Quaternion& startRot,
                                                                  GizmoAxis axis,
                                                                  float angleRad,
                                                                  bool snap,
                                                                  bool worldSpace = false) noexcept;

            //! スクリーンのドラッグ量を、スケールへ足し引きする変化量に変換する
            [[nodiscard]] static float ScreenDragToScaleAmount(Vector2 axisDir2d, Vector2 dragPixels) noexcept;

            //! 指定された軸とスケール変化量に基づく、新しいスケールベクトルを計算する
            [[nodiscard]] static Vector3 ComputeScale(const Vector3& startScale,
                                                          GizmoAxis axis,
                                                          float amount,
                                                          bool snap) noexcept;

            //! 入力されたキーに応じたギズモツール種別を返す
            [[nodiscard]] static GizmoTool ToolForKey(GizmoTool current, OS::Key key) noexcept;

            //! @brief マウス座標から、クリックされたギズモのハンドルを判定して返す
            [[nodiscard]] static GizmoAxis ToolHandlePick(const Vector3& gizmoOrigin,
                                                          const Quaternion& rotation,
                                                          GizmoTool tool,
                                                          Vector2 mouse2d,
                                                          const Matrix& viewProjection,
                                                          Size2D viewport) noexcept;

        private:
            OS::Input* m_input = nullptr;

            std::span<SceneActor* const> m_objects{};
            std::span<const uint8> m_pickable{}; //!< 選択の優先度。1 のアクターを先に選ぶ

            bool m_active = false;
            GizmoTool m_tool = GizmoTool::Move;
            GizmoSpace m_space = GizmoSpace::Local;
            Transform* m_selected = nullptr;

            bool m_dragging = false;
            GizmoAxis m_dragAxis = GizmoAxis::None;
            Vector2 m_dragStartScreen{}; //!< ドラッグ開始時のスクリーン座標
            TransformState m_dragBefore{};   //!< ドラッグ開始時の 位置・回転・スケール
        };
    } // namespace Editor
} // namespace NS

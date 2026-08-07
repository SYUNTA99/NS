#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/VirtualCameraComponent.h"

#include <optional>
#include <vector>

namespace NS::Object
{
    class CameraComponent;

    /// @brief 仮想カメラ群を束ね、選ばれた 1 個の pose を実カメラへ流す
    /// @details 実 CameraComponent を 1 個参照する
    /// 登録済み VirtualCameraComponent のうち active かつ最高 VcamPriority のものを毎フレーム選ぶ
    /// EvaluatePose(alpha) を実カメラへ書く
    /// 描画 / aspect 設定 / PlayerInput の forward 取得もこの Brain 経由に集約する
    /// active 切替は SetBlendDuration 秒の ease-in-out で旧 pose から繋ぎ、0 で即時カット
    /// 依存: NS::Math, NS::Object::Component / CameraComponent / VirtualCameraComponent
    class CameraBrainComponent : public Component
    {
    public:
        CameraBrainComponent() noexcept;

        /// 同じ GameObject に乗る実カメラをここで解決する。見つからなければ Evaluate は何もしない
        void OnStart() override;

        /// 候補 vcam を登録する。null と重複は無視する。寿命は呼出側が支配する非所有参照
        void AddVirtualCamera(VirtualCameraComponent* vcam);

        /// 登録済み vcam を外す。未登録と null は無視する。外した vcam が active 中なら選び直す
        /// 寿命を呼出側が握る area camera を破棄する前に呼んで無効参照を防ぐ
        void RemoveVirtualCamera(VirtualCameraComponent* vcam) noexcept;

        /// active 切替時のブレンド秒数。0 以下で即時カット。負値は 0 に丸める
        void SetBlendDuration(float seconds) noexcept;
        [[nodiscard]] float BlendDuration() const noexcept { return m_blendDuration; }

        /// fixed step で active 切替を検出しブレンドタイマーを進める。描画はしない
        void OnUpdate() override;

        /// 現在の active vcam の EvaluatePose(alpha) を実カメラへ書く。ブレンド中なら旧 pose と補間する
        /// fixed step は alpha=1、render は FrameTimer::Alpha() を渡す。タイマーは進めない
        void Evaluate(float alpha) noexcept;

        /// Evaluate 後に有効。選ばれている vcam を返し、無ければ nullptr
        [[nodiscard]] VirtualCameraComponent* ActiveVirtualCamera() const noexcept { return m_active; }

        /// 指定 pose を旧 pose としてブレンドを開始する。active な vcam が居ない状態からの切替も繋がる
        /// editor がプレイ突入時に自由視点の pose から追従カメラへ繋ぐのに使う
        void BeginBlendFrom(const CameraPose& pose) noexcept;

        /// 直近 Evaluate が実カメラへ書いた pose。editor が編集復帰時のブレンド始点に読む
        [[nodiscard]] const CameraPose& LastPose() const noexcept { return m_lastPose; }

        /// @brief 登録済みから priority 最高の vcam の pose を返します。(候補無しは nullopt)
        /// @details active は問わず選ぶ。ゲーム視点を別ビューへ映す用で実カメラには触れない
        [[nodiscard]] std::optional<CameraPose> EvaluateTopPose(float alpha) const noexcept;

        /// 実カメラへの素通しアクセサ。描画 / 半透明ソート / PlayerInput forward の接続先
        [[nodiscard]] NS::Math::Matrix ViewProjection() const noexcept;
        [[nodiscard]] NS::Math::Vector3 ForwardHorizontal() const noexcept;
        [[nodiscard]] CameraComponent* Camera() const noexcept { return m_camera; }

        // vcam 切替ブレンド秒を Inspector へ公開する。 負クランプを保つため setter 経由で書く
        NS_REFLECT_BEGIN(CameraBrainComponent, Component)
        NS_REFLECT_ACCESSOR(float, "ブレンド秒数", BlendDuration(), SetBlendDuration)
        NS_REFLECT_END()

    private:
        [[nodiscard]] VirtualCameraComponent* SelectActive() const noexcept;

        CameraComponent* m_camera = nullptr;          // 同じ GameObject に乗る実カメラ (非所有)
        std::vector<VirtualCameraComponent*> m_vcams; // 登録済み vcam 候補 (非所有)
        VirtualCameraComponent* m_active = nullptr;   // 現在選ばれている vcam

        CameraPose m_lastPose{};       // 直近 Evaluate が実カメラへ書いた pose。切替時のブレンド始点になる
        CameraPose m_blendFrom{};      // ブレンド開始時にスナップした旧 pose
        float m_blendDuration = 0.35f; // active 切替のブレンド秒数
        float m_blendElapsed = 0.0f;   // ブレンド開始からの経過秒
        bool m_blending = false;       // ブレンド進行中か
    };
} // namespace NS::Object

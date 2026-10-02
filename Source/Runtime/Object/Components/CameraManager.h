#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/Components/CameraModifier.h"
#include "Runtime/Object/Components/VirtualCamera.h"
#include "Runtime/Object/ITickable.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace NS::Obj
{
    class CameraComponent;

    //! @brief シーンに 1 つのカメラの管理役。仮想カメラ群を束ね、選ばれた 1 個の pose に効果を掛けて実カメラへ流す
    //! @details UE の PlayerCameraManager、オデッセイの CameraDirector に当たる。部品と Actor は IUseCamera
    //! から引く。登録済み VirtualCamera のうち active かつ最高 VcamPriority のものを毎フレーム選ぶ。active 切替は
    //! m_blendDuration 秒の ease-in-out で旧 pose から繋ぐ。揺れや寄りのような効果はモディファイア
    //! (CameraModifier) として積み、ブレンドの後に Order の順で掛ける。効果を足す側は管理役を触らずモディファイアを 1
    //! つ積むだけでよい。描き終えたモディファイアは管理役が外す。描画 / aspect 設定 / PlayerInput の forward
    //! 取得もこの管理役経由に集約する。シーンがカメラの段の Actor を回した直後に OnTick を呼ぶので、vcam を動かす
    //! 追従カメラ (カメラの段) より後ろで選び直す
    //! 依存: NS::Core, NS::Obj::Component / CameraComponent / VirtualCamera / CameraModifier
    class CameraManager : public NS::Core::NonCopyable, public ITickable
    {
    public:
        CameraManager() noexcept;

        //! 姿勢を書き込む実カメラを差し替える。非所有で、nullptr は書き込む先が無い状態
        void SetCamera(CameraComponent* camera) noexcept { m_camera = camera; }

        //! 候補 vcam を登録する。null と重複は無視する。寿命は呼出側が支配する非所有参照
        void AddVirtualCamera(VirtualCamera* vcam);

        //! 登録済み vcam を外す。未登録と null は無視する。外した vcam が active 中なら選び直す
        //! 寿命を呼出側が握る vcam を破棄する前に呼んで無効参照を防ぐ
        void RemoveVirtualCamera(VirtualCamera* vcam) noexcept;

        //! fixed step で active 切替を検出しブレンドタイマーを進める。描画はしない
        void OnUpdate();
        void OnTick() override { OnUpdate(); }

        //! 現在の active vcam の EvaluatePose(alpha) を実カメラへ書く。ブレンド中なら旧 pose と補間する
        //! 呼ぶのは描画だけで、Scene が決めた割合を渡す
        //! 固定ステップの間の実カメラは最後の描画の姿勢のまま
        //! タイマーは進めない
        void Evaluate(float alpha) noexcept;

        //! Evaluate 後に有効。選ばれている vcam を返し、無ければ nullptr
        [[nodiscard]] VirtualCamera* ActiveVirtualCamera() const noexcept { return m_active; }

        //! 指定 pose を旧 pose としてブレンドを開始する。active な vcam が居ない状態からの切替も繋がる
        //! editor がプレイ突入時に自由視点の pose から追従カメラへ繋ぐのに使う
        void BeginBlendFrom(const CameraPose& pose) noexcept;

        //! 直近 Evaluate が実カメラへ書いた pose。editor が編集復帰時のブレンド始点に読む
        [[nodiscard]] const CameraPose& LastPose() const noexcept { return m_lastPose; }

        //! @brief モディファイアを積む
        //! @details 同じ種類の印 (Kind) の物が積まれていれば外してから積む。null は何もしない
        //! 積んだ後に初めて来る OnUpdate ではフレームを進めない。積んだフレームに最初の姿を描く
        //! @return 積んだ場合 true、null で何もしなかった場合は false
        bool AddModifier(std::unique_ptr<CameraModifier> modifier);

        //! 種類の印が kind のモディファイアを外す
        void RemoveModifiers(const void* kind) noexcept;

        //! 積んだモディファイアを全て外す。プレイを終えた後の視点に効果が残らないようにする
        void ClearModifiers() noexcept;

        //! 型 T (StaticKind を持つモディファイア) の積まれている物。無ければ nullptr
        template <class T> [[nodiscard]] const T* FindModifier() const noexcept
        {
            for (const std::unique_ptr<CameraModifier>& modifier : m_modifiers)
            {
                if (modifier->Kind() == T::StaticKind())
                {
                    return static_cast<const T*>(modifier.get());
                }
            }
            return nullptr;
        }

        //! @brief 画面揺れを始める。揺れの途中なら新しい設定の最初の振れから始め直す
        //! @details 揺れのモディファイアを作って積む補助。最初の横の振れは、始めた時のカメラの右と
        //! firstSideDirection の内積が負なら左、それ以外は右へ向く
        //! @return 揺れを始めた場合 true、壊れた設定で何も変えなかった場合は false
        bool StartShake(const CameraShakeDesc& desc);

        //! 今のフレームの揺れのずれ。x がカメラの右、y が上 (m)。揺れていない時は 0
        [[nodiscard]] NS::Core::Vector2 ShakeOffset() const noexcept;

        //! @brief 寄りと傾きを始める。戻しの途中なら新しい倍率と傾きから始め直す
        //! @details 寄りと傾きのモディファイアを作って積む補助。傾きの向きは、始めた時のカメラの右と
        //! rollDirection の内積が負なら上端を左へ、それ以外は右へ倒す
        //! @return 始めた場合 true、壊れた設定で何も変えなかった場合は false
        bool StartZoomRoll(const CameraZoomRollDesc& desc);

        //! 今のフレームの寄りと傾き。始めていない時と戻し終えた後は倍率 1・傾き 0
        [[nodiscard]] CameraZoomRoll ZoomRoll() const noexcept;

        //! @brief 今のカメラの画面で direction が右と左のどちらの側かを返す
        //! @details 実カメラの水平の前から作った右と direction
        //! の内積で決める。揺れの最初の横の向きと傾きの向きもこの決まり
        //! @param[in] direction 世界の向き
        //! @return 内積が負の場合 -1、それ以外の場合は 1
        [[nodiscard]] float SideSignOf(const NS::Core::Vector3& direction) const noexcept;

        //! @brief Evaluate が実カメラへ書く姿勢を、書かずに返す
        //! @details 仮想カメラ、ブレンド、積んだモディファイアの順に合成する
        //! @param[in] alpha 補間の割合
        //! @return 合成した姿勢。選べる仮想カメラが無い場合は nullopt
        [[nodiscard]] std::optional<CameraPose> ComposePose(float alpha) const noexcept;

        //! @brief 登録済みから priority 最高の vcam の pose を返す。候補が無ければ nullopt
        //! @details active は問わず選ぶ。ゲーム視点を別ビューへ映す用で実カメラには触れない
        [[nodiscard]] std::optional<CameraPose> EvaluateTopPose(float alpha) const noexcept;

        //! 実カメラへの素通しアクセサ。描画 / 半透明ソート / PlayerInput forward の接続先
        [[nodiscard]] NS::Core::Matrix ViewProjection() const noexcept;
        [[nodiscard]] NS::Core::Vector3 ForwardHorizontal() const noexcept;
        [[nodiscard]] CameraComponent* Camera() const noexcept { return m_camera; }

    private:
        [[nodiscard]] VirtualCamera* SelectActive() const noexcept;

        CameraComponent* m_camera = nullptr; // Scene が持つ実カメラ (非所有)
        std::vector<VirtualCamera*> m_vcams; // 登録済み vcam 候補 (非所有)
        VirtualCamera* m_active = nullptr;   // 現在選ばれている vcam

        CameraPose m_lastPose{};       // 直近 Evaluate が実カメラへ書いた pose。切替時のブレンド始点になる
        CameraPose m_blendFrom{};      // ブレンド開始時にスナップした旧 pose
        float m_blendDuration = 0.35f; // active 切替のブレンド秒数
        float m_blendElapsed = 0.0f;   // ブレンド開始からの経過秒
        bool m_blending = false;       // ブレンド進行中か

        std::vector<std::unique_ptr<CameraModifier>> m_modifiers; // 積んだ効果。Order 昇順、同じ順は積んだ順
    };
} // namespace NS::Obj

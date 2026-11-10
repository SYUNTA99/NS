#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

#include <memory>

namespace NS
{
    class CameraManager;
    class IGetCamera;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! delayFrames の後に倍率と傾きを適用し、holdFrames の間保ち、returnFrames で滑らかに元へ戻す
        struct CameraZoomRollDesc
        {
            float zoom = 1.0f;                           // 1 で等倍
            float rollDegrees = 0.0f;                    // 視線軸まわりの傾き (度)
            NS::Vector3 rollDirection{1.0f, 0.0f, 0.0f}; // 画面上端を倒す側を決める世界方向
            int holdFrames = 0;                          // 開始フレームを含む保持フレーム数
            int returnFrames = 0;
            int delayFrames = 0;
        };

        struct CameraZoomRoll
        {
            float zoom = 1.0f;
            float rollDegrees = 0.0f; // 視線軸まわりの傾き (度、正は画面上端をカメラの右へ倒す向き)
        };

        //! 寄りと傾き。寄りは視野角、傾きは視線の軸まわりの上の向きで掛け、視線ベクトルは変えない
        class CameraZoomRollModifier final : public NS::CameraModifier
        {
        public:
            //! rollSign が負なら上端を左へ、それ以外は右へ倒す
            //! 非有限値・zoom < 1・負のフレーム数の場合は nullptr を返す
            [[nodiscard]] static std::unique_ptr<CameraZoomRollModifier> Create(const CameraZoomRollDesc& desc,
                                                                                float rollSign);

            [[nodiscard]] static const void* StaticKind() noexcept;
            [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }
            [[nodiscard]] int Order() const noexcept override { return NS::k_CameraLensOrder; }

            void Modify(NS::CameraPose& pose, const NS::CameraAxes& axes) const noexcept override;
            [[nodiscard]] bool IsFinished() const noexcept override;

            //! 現在のフレームの寄りと傾き。完了後は zoom = 1, rollDegrees = 0
            [[nodiscard]] CameraZoomRoll Current() const noexcept;

        private:
            CameraZoomRollModifier() noexcept = default;

            [[nodiscard]] int ZoomFrame() const noexcept { return Frame() - m_delayFrames; }

            CameraZoomRoll m_full{};
            int m_holdFrames = 0;
            int m_returnFrames = 0;
            int m_delayFrames = 0;
        };

        //! 沈む・震える・保持・跳ね返りの縦揺れ。既定の 0 は設定の無い値で、当たりの項目 (HitCameraSinkInfo) が全部書く。高さ referenceHeightPixels の画面の画素で、下が負
        struct CameraSinkDesc
        {
            float bottomPixels = 0.0f;   // 底の深さ (画素、0 以上)
            int sinkFrames = 0;          // 開始フレームを含む底到達までのフレーム数。1 以上
            float tremblePixels = 0.0f;  // 震えの初期振幅 (画素)
            int trembleFrames = 0;
            int tremblePeriodFrames = 0; // 震え 1 往復のフレーム数。1 以上
            int bounceStartFrame = 0;    // 開始フレームを 0 とした跳ね返り開始フレーム
            float overshootRatio = 0.0f; // 底の深さに対する上方向オーバーシュート比 (0 より大きく 1 未満)
            int bouncePeriodFrames = 0;  // 跳ね返り 1 往復のフレーム数。1 以上
            int frames = 0;              // 開始フレームを含む総フレーム数
            //! 画素を測る画面の高さ。画面の座標 (縦 -1〜1) へ直す時の割る数で、解像度が違っても同じ割合だけ動く。正の数
            float referenceHeightPixels = 0.0f;
        };

        //! 開始を 0 とした frame の縦ずれ (画素、下が負)。範囲外は 0
        [[nodiscard]] float CameraSinkPixelsAt(const CameraSinkDesc& desc, int frame) noexcept;

        //! 射影後の画面を縦にずらす。カメラとの距離や入力方向には影響しない
        class CameraSinkModifier final : public NS::CameraModifier
        {
        public:
            //! 非有限値・負の深さや振幅・frames <= 0・周期 < 1・overshootRatio が (0, 1) 外・画面の高さが正でない場合は
            //! nullptr を返す
            [[nodiscard]] static std::unique_ptr<CameraSinkModifier> Create(const CameraSinkDesc& desc);

            [[nodiscard]] static const void* StaticKind() noexcept;
            [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }
            [[nodiscard]] int Order() const noexcept override { return NS::k_CameraShakeOrder; }

            void Modify(NS::CameraPose& pose, const NS::CameraAxes& axes) const noexcept override;
            [[nodiscard]] bool IsFinished() const noexcept override;
            [[nodiscard]] bool IsShake() const noexcept override { return true; }

            //! 現在のフレームの縦のずれ (画素、下が負)。完了後は 0
            [[nodiscard]] float Pixels() const noexcept;

            //! 次の Advance フレームから跳ね返りを開始する (設定の bounceStartFrame より遅くはしない)
            void Release() noexcept { m_releasePending = true; }

        private:
            CameraSinkModifier() noexcept = default;
            void Advance() noexcept override;

            CameraSinkDesc m_desc{};
            bool m_releasePending = false;
        };

        //! 位置と注視点を同じだけずらす設定
        struct CameraNudgeDesc
        {
            NS::Vector3 direction{}; // ずらす世界方向 (単位ベクトル)
            NS::Curve distance{};    // 開始フレームを 0 とした移動距離 (m、負は逆方向)
            int frames = 0;          // 開始フレームを含む総フレーム数
            bool onScreen = false;   // true ならカメラの右・上平面へ射影した方向へずらす
            //! onScreen の時、画面へ写した向きの長さがこれ以下なら動かさない。向きが画面の奥をほぼ真っすぐ指していて、
            //! 画面の上の向きが決まらないため
            float minScreenShare = 0.0f;
        };

        //! 位置と注視点を同じだけずらす。画面射影時の方向成分が微小なら動かさない
        //! 世界方向と画面方向は併用でき、同じ方式の再指定は前の効果を置き換える
        class CameraNudgeModifier final : public NS::CameraModifier
        {
        public:
            //! 非有限値・direction が単位長でない・frames <= 0・onScreen で minScreenShare が正でない場合は nullptr を返す
            [[nodiscard]] static std::unique_ptr<CameraNudgeModifier> Create(const CameraNudgeDesc& desc);

            [[nodiscard]] static const void* KindFor(bool onScreen) noexcept;
            [[nodiscard]] const void* Kind() const noexcept override { return KindFor(m_desc.onScreen); }
            [[nodiscard]] int Order() const noexcept override { return NS::k_CameraShakeOrder; }

            void Modify(NS::CameraPose& pose, const NS::CameraAxes& axes) const noexcept override;
            [[nodiscard]] bool IsFinished() const noexcept override;
            [[nodiscard]] bool IsShake() const noexcept override { return true; }

            [[nodiscard]] const CameraNudgeDesc& Desc() const noexcept { return m_desc; }

            //! 現在のフレームの移動距離 (m)。完了後は 0
            [[nodiscard]] float Distance() const noexcept;

        private:
            CameraNudgeModifier() noexcept = default;

            CameraNudgeDesc m_desc{};
        };

        //! 寄りと傾きを始める。傾きの向きは、始めた時のカメラの右と rollDirection の内積が負なら上端を左へ、
        //! それ以外は右へ倒す。user を頼んだ物にし、戻しの途中なら新しい倍率と傾きから始め直す
        //! @return 始めた場合 true。カメラの管理役が無いか、設定が壊れている場合は false
        bool StartCameraZoomRoll(const NS::IGetCamera* user, const CameraZoomRollDesc& desc);

        //! 今のフレームの寄りと傾き。始めていない時と戻し終えた後は倍率 1・傾き 0
        [[nodiscard]] CameraZoomRoll CurrentCameraZoomRoll(const NS::CameraManager& cameras) noexcept;

        //! user が頼んだ沈む揺れを跳ね返す。沈みが無ければ何もしない。跳ね返りは次に進めたフレームから始まる
        void ReleaseCameraSink(const NS::IGetCamera* user) noexcept;
    } // namespace Level
} // namespace GL

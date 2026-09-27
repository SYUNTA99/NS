#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/VirtualCamera.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace NS::Obj
{
    class CameraComponent;

    //! @brief 画面揺れの形の設定
    //! @details ずれはカメラの右と上の向きへの平行移動
    //! 大きさは始めたフレームが最大で、残りのフレーム数に比例して直線に減る
    //! 横と縦の向きはそれぞれ 1〜longestFlipFrames フレームごとに入れ替わる
    //! longestFlipFrames が 2 以上なら続けて同じ間隔にならない
    //! 間隔は seed から選び、横と縦は別の並びになる。縦の最初の振れは下
    struct CameraShakeDesc
    {
        float sideAmplitude = 0.0f; // 最初の振れの横の大きさ (m)
        float upAmplitude = 0.0f;   // 最初の振れの縦の大きさ (m)
        int frames = 0;             // 揺れを描くフレーム数。始めたフレームを含む
        int longestFlipFrames = 1;  // 向きが入れ替わるまでの最長フレーム数。横と縦の両方に掛かる
        NS::Core::Vector3 firstSideDirection{1.0f, 0.0f, 0.0f}; // 最初の横の振れを向ける世界の向き
        std::uint32_t seed = 0;                                 // 入れ替わりの間隔を選ぶ種
    };

    //! @brief 寄りと傾きの設定
    //! @details 始めたフレームから倍率と傾きを全部入れ、holdFrames の間保ち、returnFrames で滑らかに元へ戻す
    struct CameraZoomRollDesc
    {
        float zoom = 1.0f;                                 // 画面に写る大きさの倍率。1 で寄らない
        float rollDegrees = 0.0f;                          // 視線の軸まわりの傾き (度)
        NS::Core::Vector3 rollDirection{1.0f, 0.0f, 0.0f}; // 画面の上端を倒す側を決める世界の向き
        int holdFrames = 0;                                // 倍率と傾きを保つフレーム数。始めたフレームを含む
        int returnFrames = 0;                              // 元へ戻すフレーム数
    };

    //! @brief 今のフレームの寄りと傾き
    struct CameraZoomRoll
    {
        float zoom = 1.0f;        // 画面に写る大きさの倍率
        float rollDegrees = 0.0f; // 視線の軸まわりの傾き (度)。正は画面の上端をカメラの右へ倒す向き
    };

    //! @brief 仮想カメラ群を束ね、選ばれた 1 個の pose を実カメラへ流す
    //! @details 実 CameraComponent を 1 個参照する
    //! 登録済み VirtualCamera のうち active かつ最高 VcamPriority のものを毎フレーム選ぶ
    //! EvaluatePose(alpha) を実カメラへ書く
    //! 描画 / aspect 設定 / PlayerInput の forward 取得もこの Brain 経由に集約する
    //! active 切替は SetBlendDuration 秒の ease-in-out で旧 pose から繋ぎ、0 で即時カット
    //! 帯は LateUpdate + 60。vcam を供給する追従カメラの LateUpdate + 50 より後ろで選び直す
    //! 依存: NS::Core, NS::Obj::Component / CameraComponent / VirtualCamera
    class CameraBrain : public Component
    {
    public:
        CameraBrain() noexcept;

        //! 同じ GameObject に乗る実カメラをここで解決する。見つからなければ Evaluate は何もしない
        void OnStart() override;

        //! 候補 vcam を登録する。null と重複は無視する。寿命は呼出側が支配する非所有参照
        void AddVirtualCamera(VirtualCamera* vcam);

        //! 登録済み vcam を外す。未登録と null は無視する。外した vcam が active 中なら選び直す
        //! 寿命を呼出側が握る vcam を破棄する前に呼んで無効参照を防ぐ
        void RemoveVirtualCamera(VirtualCamera* vcam) noexcept;

        //! active 切替時のブレンド秒数。0 以下で即時カット。負値は 0 に丸める
        void SetBlendDuration(float seconds) noexcept;
        [[nodiscard]] float BlendDuration() const noexcept { return m_blendDuration; }

        //! fixed step で active 切替を検出しブレンドタイマーを進める。描画はしない
        void OnUpdate() override;

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

        //! @brief 画面揺れを始める。揺れの途中なら新しい設定の最初の振れから始め直す
        //! @details StartShake の後に初めて来る OnUpdate ではフレームを進めない。始めたフレームに最初の振れを描く
        //! 最初の横の振れは、始めた時のカメラの右と firstSideDirection の内積が負なら左、それ以外は右へ向く
        //! 非数・負の振れ幅・フレーム数 0 以下・最長 1 未満は壊れた設定
        //! @param[in] desc 揺れの形
        //! @return 揺れを始めた場合 true、壊れた設定で何も変えなかった場合は false
        bool StartShake(const CameraShakeDesc& desc) noexcept;

        //! @brief 今のフレームの揺れのずれを返す
        //! @return x がカメラの右、y がカメラの上の向きのずれ (m)。揺れていない時は 0
        [[nodiscard]] NS::Core::Vector2 ShakeOffset() const noexcept;

        //! @brief 寄りと傾きを始める。戻しの途中なら新しい倍率と傾きから始め直す
        //! @details StartZoomRoll の後に初めて来る OnUpdate ではフレームを進めない
        //! 傾きの向きは、始めた時のカメラの右と rollDirection の内積が負なら上端を左へ、それ以外は右へ倒す
        //! 倍率 1 未満か非数・傾きか向きが非数・フレーム数が負は壊れた設定
        //! @param[in] desc 寄りと傾き
        //! @return 始めた場合 true、壊れた設定で何も変えなかった場合は false
        bool StartZoomRoll(const CameraZoomRollDesc& desc) noexcept;

        //! @brief 今のフレームの寄りと傾きを返す。始めていない時と戻し終えた後は倍率 1・傾き 0
        [[nodiscard]] CameraZoomRoll ZoomRoll() const noexcept;

        //! @brief 揺れと寄りと傾きを止める
        void StopShakeAndZoomRoll() noexcept;

        //! @brief 今のカメラの画面で direction が右と左のどちらの側かを返す
        //! @details 実カメラの水平の前から作った右と direction
        //! の内積で決める。揺れの最初の横の向きと傾きの向きもこの決まり
        //! @param[in] direction 世界の向き
        //! @return 内積が負の場合 -1、それ以外の場合は 1
        [[nodiscard]] float SideSignOf(const NS::Core::Vector3& direction) const noexcept;

        //! @brief Evaluate が実カメラへ書く姿勢を、書かずに返す
        //! @details 仮想カメラ、ブレンド、揺れ、寄りと傾きの順に合成する
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

        // vcam 切替ブレンド秒を Inspector へ公開する。負クランプを保つため setter 経由で書く
        NS_REFLECT_BEGIN(CameraBrain, Component)
        NS_REFLECT_ACCESSOR(float, "ブレンド秒数", BlendDuration(), SetBlendDuration)
        NS_REFLECT_END()

    private:
        [[nodiscard]] VirtualCamera* SelectActive() const noexcept;

        CameraComponent* m_camera = nullptr; // 同じ GameObject に乗る実カメラ (非所有)
        std::vector<VirtualCamera*> m_vcams; // 登録済み vcam 候補 (非所有)
        VirtualCamera* m_active = nullptr;   // 現在選ばれている vcam

        CameraPose m_lastPose{};       // 直近 Evaluate が実カメラへ書いた pose。切替時のブレンド始点になる
        CameraPose m_blendFrom{};      // ブレンド開始時にスナップした旧 pose
        float m_blendDuration = 0.35f; // active 切替のブレンド秒数
        float m_blendElapsed = 0.0f;   // ブレンド開始からの経過秒
        bool m_blending = false;       // ブレンド進行中か

        std::vector<NS::Core::Vector2> m_shakeOffsets; // 揺れのフレームごとのずれ (m)。x が右、y が上
        int m_shakeFrame = 0;                          // m_shakeOffsets の今のフレームの番号
        bool m_shakeStartedThisFrame = false;          // 次の OnUpdate でフレームを進めないか

        CameraZoomRoll m_zoomRollFull{};         // 保つ間の倍率と向きを付けた傾き
        int m_zoomRollHoldFrames = 0;            // 保つフレーム数
        int m_zoomRollReturnFrames = 0;          // 戻すフレーム数
        int m_zoomRollFrame = 0;                 // 始めたフレームからの番号。保つと戻すの和に達したら終わり
        bool m_zoomRollStartedThisFrame = false; // 次の OnUpdate でフレームを進めないか
    };
} // namespace NS::Obj

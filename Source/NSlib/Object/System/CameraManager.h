#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/SubObjects/CameraModifier.h"
#include "NSlib/Object/SubObjects/VirtualCamera.h"
#include "NSlib/Object/System/SceneSystem.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace NS::Obj
{
    class SceneCamera;

    //! @brief 有効な仮想カメラを優先度で選び、切り替えを補間して効果を重ねる
    //! @details 遊びの判定は効果を含まない ViewPose を使う。実カメラへの反映は描画時に行う
    class CameraManager : public SceneSystem
    {
    public:
        CameraManager() noexcept;

        //! 姿勢を書き込む実カメラを差し替える。非所有で、nullptr は書き込む先が無い状態
        void SetCamera(SceneCamera* camera) noexcept { m_camera = camera; }

        //! 候補 vcam を登録する。空と重複は無視する。所有せず、呼び出し側が寿命を保つ
        void AddVirtualCamera(VirtualCamera* vcam);

        //! 登録済み vcam を外す。未登録と空は無視する。使用中のカメラを外したら選び直す
        //! 寿命を呼出側が握る vcam を破棄する前に呼んで無効参照を防ぐ
        void RemoveVirtualCamera(VirtualCamera* vcam) noexcept;

        [[nodiscard]] std::size_t VirtualCameraCount() const noexcept { return m_vcams.size(); }

        //! active 切替時のブレンド秒数。0 以下で即時カット。負値は 0 に丸める
        void SetBlendDuration(float seconds) noexcept;
        [[nodiscard]] float BlendDuration() const noexcept { return m_blendDuration; }

        //! 固定ステップで使用するカメラの切り替えを検出し、補間の時間を進める。描画はしない
        void OnTick() override;

        //! 補間した姿勢と効果を実カメラへ反映する。時間は進めない
        //! 遊びの判定は ViewPose を使い、実カメラを読まない
        void Evaluate(float alpha) noexcept;

        //! Evaluate 後に有効。選ばれている vcam を返し、無ければ nullptr
        [[nodiscard]] VirtualCamera* ActiveVirtualCamera() const noexcept { return m_active; }

        //! 指定 pose を旧 pose としてブレンドを開始する。active な vcam が居ない状態からの切替も繋がる
        //! editor がプレイ突入時に自由視点の pose から追従カメラへ繋ぐのに使う
        void BeginBlendFrom(const CameraPose& pose) noexcept;

        //! 直近 Evaluate が実カメラへ書いた pose。editor が編集復帰時のブレンド始点に読む
        [[nodiscard]] const CameraPose& LastPose() const noexcept { return m_lastPose; }

        //! @brief モディファイアを積む
        //! @details 同じ Kind があれば置き換える。参照先がなければ何もしない
        //! 積んだ後に初めて来る OnTick ではフレームを進めない。積んだフレームに最初の姿を描く
        //! @return 積んだ場合 true、参照先がなく処理しなかった場合は false
        bool AddModifier(std::unique_ptr<CameraModifier> modifier);

        //! 種類の印が kind のモディファイアを外す
        void RemoveModifiers(const void* kind) noexcept;

        //! 積んだモディファイアを全て外す。プレイを終えた後の視点に効果が残らないようにする
        void ClearModifiers() noexcept;

        //! kind は検索する種類の識別値。省略時は T::StaticKind、未登録なら nullptr
        template <class T> [[nodiscard]] const T* FindModifier(const void* kind = T::StaticKind()) const noexcept
        {
            for (const std::unique_ptr<CameraModifier>& modifier : m_modifiers)
            {
                if (modifier->Kind() == kind)
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
        [[nodiscard]] NS::Vector2 ShakeOffset() const noexcept;

        //! @brief 寄りと傾きを始める。戻しの途中なら新しい倍率と傾きから始め直す
        //! @details 寄りと傾きのモディファイアを作って積む補助。傾きの向きは、始めた時のカメラの右と
        //! rollDirection の内積が負なら上端を左へ、それ以外は右へ倒す
        //! @return 始めた場合 true、壊れた設定で何も変えなかった場合は false
        bool StartZoomRoll(const CameraZoomRollDesc& desc);

        //! 今のフレームの寄りと傾き。始めていない時と戻し終えた後は倍率 1・傾き 0
        [[nodiscard]] CameraZoomRoll ZoomRoll() const noexcept;

        //! @brief トラウマを足す。トラウマの揺れが積まれていなければ積む
        //! @details 揺れの途中なら今のトラウマに足す (上限 1)。続けて当てた時に揺れが重なる
        //! @return 足した場合 true、量か形が壊れていて何も変えなかった場合は false
        bool AddTrauma(const CameraTraumaDesc& desc);

        //! @brief このフレームのトラウマを少なくとも level に保つ。トラウマの揺れが積まれていなければ積む
        //! @details 溜めのように続く揺れが毎フレーム呼ぶ。呼ばなかったフレームから減り始める
        //! @return 保った場合 true、量か形が壊れていて何も変えなかった場合は false
        bool HoldTrauma(float level, const CameraTraumaShape& shape);

        //! 今のトラウマ。トラウマの揺れが無い時は 0
        [[nodiscard]] float Trauma() const noexcept;

        //! @brief 揺れの振れ幅に掛ける設定の倍率を置く。0〜1 に収め、非数は受けない
        void SetShakeScale(float scale) noexcept;
        [[nodiscard]] float ShakeScale() const noexcept { return m_shakeScale; }

        //! @brief 効果を含まないカメラの右に対する direction の側を返す
        //! @return 内積が負の場合 -1、それ以外の場合は 1
        [[nodiscard]] float SideSignOf(const NS::Vector3& direction) const noexcept;

        //! @brief Evaluate が実カメラへ書く姿勢を、書かずに返す
        //! @details 仮想カメラ、ブレンド、積んだモディファイアの順に合成する
        //! @param[in] alpha 補間の割合
        //! @return 合成した姿勢。選べる仮想カメラが無い場合は nullopt
        [[nodiscard]] std::optional<CameraPose> ComposePose(float alpha) const noexcept;

        //! 固定更新の視点を効果なしで返す。入力や狙いの判定に使い、描画の補間割合に依存しない
        //! 選べる仮想カメラがなければ nullopt
        [[nodiscard]] std::optional<CameraPose> ViewPose() const noexcept;

        //! @brief 登録済みから priority 最高の vcam の pose を返す。候補が無ければ nullopt
        //! @details active は問わず選ぶ。ゲーム視点を別ビューへ映す用で実カメラには触れない
        [[nodiscard]] std::optional<CameraPose> EvaluateTopPose(float alpha) const noexcept;

        //! 実カメラの視点と投影の行列。実カメラが無ければ単位行列
        [[nodiscard]] NS::Matrix ViewProjection() const noexcept;

        //! ViewPose の視線を水平に正規化する。視点か水平成分がなければ +Z
        [[nodiscard]] NS::Vector3 ForwardHorizontal() const noexcept;

        //! 姿勢を書き込む実カメラ。無ければ nullptr
        [[nodiscard]] SceneCamera* Camera() const noexcept { return m_camera; }

    private:
        // 効いている vcam のうち priority 最高の物。無ければ nullptr
        [[nodiscard]] VirtualCamera* SelectActive() const noexcept;
        // 効いていない物も含めて priority 最高の vcam。無ければ nullptr
        [[nodiscard]] VirtualCamera* SelectHighest() const noexcept;

        // 積んであるトラウマの揺れ。無ければ積んで返す
        [[nodiscard]] CameraTraumaModifier* TraumaModifier();

        // 仮想カメラとブレンドまでを合成する。効果は掛けない。選べる仮想カメラが無ければ nullopt
        [[nodiscard]] std::optional<CameraPose> ComposeBeforeEffects(float alpha) const noexcept;

        SceneCamera* m_camera = nullptr;     // Scene が持つ実カメラ (非所有)
        std::vector<VirtualCamera*> m_vcams; // 登録済み vcam 候補 (非所有)
        VirtualCamera* m_active = nullptr;   // 現在選ばれている vcam

        CameraPose m_lastPose{};       // 直近 Evaluate が実カメラへ書いた pose。切替時のブレンド始点になる
        CameraPose m_blendFrom{};      // ブレンド開始時にスナップした旧 pose
        float m_blendDuration = 0.35f; // active 切替のブレンド秒数
        float m_blendElapsed = 0.0f;   // ブレンド開始からの経過秒
        bool m_blending = false;       // ブレンド進行中か
        float m_shakeScale = 1.0f;     // 揺れの振れ幅に掛ける設定の倍率

        std::vector<std::unique_ptr<CameraModifier>> m_modifiers; // 積んだ効果。Order 昇順、同じ順は積んだ順
    };
} // namespace NS::Obj

#pragma once

#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/System/SceneSystem.h"

#include <vector>

namespace NS
{
    class SceneActor;
    class Scene;

    //! @brief パッドの振動 1 回。左右のモーターの速さを始めてからのフレーム数の曲線で決め、書くフレーム数で切る
    struct HitPadVibration
    {
        Curve left{};   //!< 左のモーターの速さ 0〜1。横軸は始めたフレームを 0 にしたフレーム数。点が無ければ 0
        Curve right{};  //!< 右のモーターの速さ 0〜1。横軸は left と同じ
        int frames = 0; //!< 書くフレーム数。0 なら震わせない
    };

    //! シーン共通の振動。開始時に初期値を出し、更新ごとに実機へ書き、破棄時に停止する
    class PadRumbleDirector final : public SceneSystem
    {
    public:
        explicit PadRumbleDirector(Scene& scene);
        ~PadRumbleDirector() noexcept override;

        //! @brief 振動を始める。前の振動が残っていても、重ねた分も含めて始め直す
        //! @param[in] requester 頼んだ物。Stop で同じ物を渡すと消える
        //! @param[in] pad 振動の設定
        void Start(const SceneActor* requester, const HitPadVibration& pad);
        //! requester の振動を重ねる。振動していなければ Start と同じ
        void Blend(const SceneActor* requester, const HitPadVibration& pad);

        //! requester が頼んだ振動を外して書き直す。残りが無ければ 0 を書いて止める
        void Stop(const SceneActor* requester);

        //! 振動を 1 フレーム進めて書く
        void OnTick() override;

    private:
        // 始めてからのフレーム数に応じた速さを足してパッドへ書く。全部が長さに届いたフレームは 0 を書いて止める
        void Write();

        // 重ねた振動 1 つ。重ねたフレームの m_elapsed から進める。頼んだ物は見分けるだけで、指す先は読まない
        struct Layer
        {
            HitPadVibration pad;
            int startElapsed = 0;
            const SceneActor* requester = nullptr;
        };

        std::vector<Layer> m_layers; // 書いている振動。始めた振動と、重ねた振動
        int m_elapsed = 0;           // 振動を始めたフレームから数えたフレーム数
        bool m_running = false;      // 振動を書いている最中か
        bool m_justStarted = false;  // 振動を始めた後まだ更新を通っていないか
    };
} // namespace NS

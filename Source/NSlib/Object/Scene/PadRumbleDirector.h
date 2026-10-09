#pragma once

#include "NSlib/Object/ITickable.h"
#include "NSlib/Object/Reflection/Curve.h"
#include "NSlib/Object/Scene/SceneObjHolder.h"

#include <vector>

namespace NS::Obj
{
    class Actor;
    class Scene;

    //! @brief パッドの振動 1 回。左右のモーターの速さを始めてからのフレーム数の曲線で決め、書くフレーム数で切る
    struct HitPadVibration
    {
        Curve left{};   //!< 左のモーターの速さ 0〜1。横軸は始めたフレームを 0 にしたフレーム数。点が無ければ 0
        Curve right{};  //!< 右のモーターの速さ 0〜1。横軸は left と同じ
        int frames = 0; //!< 書くフレーム数。0 なら震わせない
    };

    //! @brief パッドの振動を進めて書く、シーンに 1 つの物
    //! @details 部品の HitReaction が頼む。頼んだフレームに最初の値を書き、次の更新から進める。
    //! 書かれなかったフレームは Gamepad::Update が 0 にするので、振動の間は毎フレーム書く。捨てる時に 0 を書く
    class PadRumbleDirector final : public ISceneObj, public ITickable
    {
    public:
        explicit PadRumbleDirector(Scene& scene);
        ~PadRumbleDirector() noexcept override;

        //! @brief 振動を始める。前の振動が残っていても、重ねた分も含めて始め直す
        //! @param[in] requester 頼んだ物。Stop で同じ物を渡すと消える
        //! @param[in] pad 振動の設定
        void Start(const Actor& requester, const HitPadVibration& pad);
        //! @brief 書いている振動に pad を重ねる
        //! @details 重ねたフレームを 0 として pad を進め、値を足す。pad の長さを過ぎたら 0 を足す。
        //! 何も書いていなければ Start と同じ
        //! @param[in] requester 頼んだ物
        //! @param[in] pad 重ねる振動
        void Blend(const Actor& requester, const HitPadVibration& pad);

        //! requester が頼んだ振動を外して書き直す。残りが無ければ 0 を書いて止める
        void Stop(const Actor& requester);

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
            const Actor* requester = nullptr;
        };

        Scene& m_scene;
        std::vector<Layer> m_layers; // 書いている振動。始めた振動と、重ねた振動
        int m_elapsed = 0;           // 振動を始めたフレームから数えたフレーム数
        bool m_running = false;      // 振動を書いている最中か
        bool m_justStarted = false;  // 振動を始めた後まだ更新を通っていないか
    };
} // namespace NS::Obj

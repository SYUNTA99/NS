#pragma once

#include "Runtime/Object/UIActor.h"

#include <cstdint>

namespace NS::Obj
{
    //! @brief 画面の最前面へ黒を重ねる暗転と明転。何のための暗転かは知らない
    //! @details 暗転する理由を知る側が開いて持つ
    //! 暗転しきったら BeginIn まで全黒を保持する。UI の段で fixed step ぶん進み、
    //! 開いた側は全黒 (IsBlack) を見て次の手を打つ。描画は世界と重ね描きの部品の後
    class ScreenFade final : public UIActor
    {
    public:
        ScreenFade() noexcept;
        NS_REFLECT_NONE(ScreenFade, UIActor)

        //! 暗転を始める。seconds かけて透明から全黒へ。演出中の呼び直しは無視する
        void BeginOut(float seconds) noexcept;

        //! 明転を始める。seconds かけて全黒から透明へ。全黒の保持中以外の呼び出しは無視する
        void BeginIn(float seconds) noexcept;

        //! 演出を捨てて透明へ戻す
        void Cancel() noexcept;

        //! dt 秒だけ進める。透明時と全黒の保持中は何もしない
        void Advance(float dt) noexcept;

        //! UI の段で fixed step ぶん進む。時間停止中は段ごと止まるので凍る
        void OnTick() override;

        //! 暗転か明転が進行中か
        [[nodiscard]] bool IsFading() const noexcept { return m_stage == Stage::Out || m_stage == Stage::In; }

        //! 暗転しきって全黒を保持中か
        [[nodiscard]] bool IsBlack() const noexcept { return m_stage == Stage::Hold; }

        //! 画面に重ねる黒の不透明度 (0=透明 / 1=全黒)
        [[nodiscard]] float Alpha() const noexcept;

        //! 暗転は他の画面の物より上に重ねる
        [[nodiscard]] int DrawOrder() const noexcept override { return 1000; }

    private:
        void SyncWidget() noexcept;
        // 演出の段階。None は透明、Hold は全黒の保持
        enum class Stage : std::uint8_t
        {
            None,
            Out,
            Hold,
            In
        };

        Stage m_stage = Stage::None;
        float m_duration = 0.0f; // 今の段階にかける秒数。BeginOut / BeginIn が決める
        float m_timer = 0.0f;    // 今の段階の経過秒。段階が変わるたび 0 へ戻す
    };

} // namespace NS::Obj

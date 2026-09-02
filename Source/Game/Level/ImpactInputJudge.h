#pragma once

namespace NS::Game::Level
{
    //! @brief 体当たりの発動種別
    enum class SlamKind
    {
        None,
        Tap,
        Charged,
    };

    //! 発動種別の日本語の表示名を返す
    [[nodiscard]] const char* SlamKindLabel(SlamKind kind) noexcept;

    //! @brief 押下時間から体当たりの発動種別を判定する
    //! @details 毎ステップ Step で保持を数え込み、この歩の発動を TakeFired で 1 回だけ引く
    //! 発動は離した歩だけで、保持がしきい値未満ならタップ、以上ならチャージを控える。両方は出ない
    struct ImpactInputJudge
    {
        //! 保持を 1 固定ステップぶん数え込む。離した歩に、保持の長さに応じてタップかチャージを控える
        void Step(bool held) noexcept;

        //! この歩に発動した種別を返し、消費する。発動が無ければ None
        [[nodiscard]] SlamKind TakeFired() noexcept;

        //! 押し始めた歩の場合 true、それ以外の場合は false
        [[nodiscard]] bool JustPressed() const noexcept;

        //! ボタンを保持している場合 true、それ以外の場合は false
        [[nodiscard]] bool IsHeld() const noexcept;

        //! 保持がチャージしきい値に達している場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCharging() const noexcept;

        //! チャージに入った歩の場合 true、それ以外の場合は false。押し直すたびにもう一度 true になる
        [[nodiscard]] bool JustStartedCharging() const noexcept;

        //! 溜めが満タンに達している場合 true、それ以外の場合は false
        [[nodiscard]] bool IsChargeFull() const noexcept;

        //! しきい値から満タンまでの溜め量を 0..1 で返す。離した歩は控えた値から読む
        [[nodiscard]] float Charge01() const noexcept;

        // 秒でなく歩数で持つのは、固定ステップの整数で数えると同じ入力列が必ず同じ判定になるため
        // 秒からの換算は CollisionInputComponent が毎ステップ入れ直すので、この既定は単体テストでだけ効く
        int chargeThresholdSteps = 12;
        int chargeMaxSteps = 60;

    private:
        // 溜め量の分子。押している間は m_heldSteps、離した歩からは控えた値を使うので押し直しまで読める
        [[nodiscard]] int ChargeSteps() const noexcept;

        int m_heldSteps = 0;
        int m_releasedSteps = 0;
        SlamKind m_fired = SlamKind::None;
    };
} // namespace NS::Game::Level

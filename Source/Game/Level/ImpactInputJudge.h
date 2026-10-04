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
    //! @details 毎ステップ Step で保持を数え込み、このフレームの発動を TakeFired で 1 回だけ引く
    //! 発動は離したフレームだけで、保持がしきい値未満なら通常突進、以上ならチャージ突進を控える。両方は出ない。
    //! ただし溜めきりから溜めすぎのフレーム数だけ押し続けると、押したままでもチャージを 1 回控えて出るのを待ち
    //! (IsAwaitingLaunch)、出たと知らされた後 (MarkLaunched) は放すまで溜め無しとして答える。待つ間と出た後の放しは
    //! 何も控えない
    struct ImpactInputJudge
    {
        //! 保持を 1 固定ステップぶん数え込む。離したフレームに、保持の長さに応じて通常突進かチャージ突進を控える
        void Step(bool held) noexcept;

        //! このフレームに発動した種別を返し、消費する。発動が無ければ None
        [[nodiscard]] SlamKind TakeFired() noexcept;

        //! 押し始めたフレームの場合 true、それ以外の場合は false
        [[nodiscard]] bool JustPressed() const noexcept;

        //! ボタンを保持している場合 true、それ以外の場合は false。溜めすぎで出た後も押している間は true
        [[nodiscard]] bool IsHeld() const noexcept;

        //! ボタンを保持していて、溜めすぎで出た後でない場合 true、それ以外の場合は false。溜めの見せ方はこれを読む
        [[nodiscard]] bool IsHoldingCharge() const noexcept;

        //! 保持がチャージしきい値に達していて、溜めすぎで出た後でない場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCharging() const noexcept;

        //! チャージに入ったフレームの場合 true、それ以外の場合は false。押し直すたびにもう一度 true になる
        [[nodiscard]] bool JustStartedCharging() const noexcept;

        //! 溜めが満タンに達している場合 true、それ以外の場合は false
        [[nodiscard]] bool IsChargeFull() const noexcept;

        //! しきい値から満タンまでの溜め量を 0..1 で返す。離したフレームは控えた値から読む。溜めすぎで出た後は 0
        [[nodiscard]] float Charge01() const noexcept;

        //! @brief 満タンから溜めすぎきるまでの深さを 0..1 で返す
        //! @details 満タンまでと溜めすぎで出た後は 0。離したフレームは控えた値から読む
        [[nodiscard]] float Overcharge01() const noexcept;

        //! @brief 満タンから数えた溜めすぎのフレーム数を返す
        //! @details 溜めすぎきった後も押している間は数え続ける。満タンまでと溜めすぎで出た後は 0。
        //! 離したフレームは控えた値から読む
        [[nodiscard]] int OverchargedSteps() const noexcept;

        //! 溜めすぎきってチャージを控え、突進が出るのを待っている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsAwaitingLaunch() const noexcept;

        //! @brief 突進が出た事を知らせる
        //! @details 出るのを待っている間だけ、放すまでの使い切りへ移す。それ以外は何もしない
        void MarkLaunched() noexcept;

        // 秒でなくフレーム数で持つのは、固定ステップの整数で数えると同じ入力列が必ず同じ判定になるため
        // 秒からの換算は Player::AdvanceCharge が毎ステップ入れ直すので、この既定は単体テストでだけ効く
        int chargeThresholdSteps = 12;
        int chargeMaxSteps = 60;
        int overchargeSteps = 180; // 満タンから押したままで出るまで

    private:
        //! 押している間の段階。放すと Charging へ戻る
        enum class HoldPhase
        {
            Charging,       // 溜めている。溜めすぎの途中を含む
            AwaitingLaunch, // 溜めすぎきってチャージを控え、出るのを待つ
            Spent,          // 溜めすぎで出た後。放すまで溜め無し
        };

        // しきい値に達しているか。溜めすぎで出た後かは見ない
        [[nodiscard]] bool ReachedThreshold() const noexcept;
        // 溜め量の分子。押している間は m_heldSteps、離したフレームからは控えた値を使うので押し直しまで読める
        [[nodiscard]] int ChargeSteps() const noexcept;

        int m_heldSteps = 0;
        int m_releasedSteps = 0;
        SlamKind m_fired = SlamKind::None;
        HoldPhase m_phase = HoldPhase::Charging;
    };
} // namespace NS::Game::Level

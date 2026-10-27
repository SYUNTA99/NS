#pragma once

namespace GL
{
    namespace Level
    {
        //! 体当たりの発動種別
        enum class SlamKind
        {
            None,
            Tap,
            Charged,
        };

        //! 発動種別の日本語の表示名を返す
        [[nodiscard]] const char* SlamKindLabel(SlamKind kind) noexcept;

        //! 押下時間から体当たりの発動種別を決める
        //! Step の後、TakeFired で一度だけ消費する。離した時の保持時間で通常・チャージを選ぶ
        //! 溜めすぎきると保持中でも一度発動を控える。MarkLaunched の後は放すまで溜めず、放しても再発動しない
        struct ImpactInputJudge
        {
            //! 保持を 1 固定ステップ進め、発動を控える
            void Step(bool held) noexcept;

            //! このフレームに発動した種別を返し、消費する。発動が無ければ None
            [[nodiscard]] SlamKind TakeFired() noexcept;

            [[nodiscard]] bool JustPressed() const noexcept;

            //! ボタンを保持している場合 true、それ以外の場合は false。溜めすぎで出た後も押している間は true
            [[nodiscard]] bool IsHeld() const noexcept;

            //! ボタンを保持していて、溜めすぎで出た後でない場合 true、それ以外の場合は false。溜めの見せ方はこれを読む
            [[nodiscard]] bool IsHoldingCharge() const noexcept;

            //! 保持がチャージしきい値に達していて、溜めすぎで出た後でない場合 true、それ以外の場合は false
            [[nodiscard]] bool IsCharging() const noexcept;

            //! チャージに入ったフレームの場合 true、それ以外の場合は false。押し直すたびにもう一度 true になる
            [[nodiscard]] bool JustStartedCharging() const noexcept;

            [[nodiscard]] bool IsChargeFull() const noexcept;

            //! しきい値から満タンまでの溜め量を 0..1 で返す。離したフレームは控えた値から読む。溜めすぎで出た後は 0
            [[nodiscard]] float Charge01() const noexcept;

            //! 満タンから溜めすぎきるまでの深さを 0..1 で返す
            //! 満タンまでと溜めすぎで出た後は 0。離したフレームは控えた値から読む
            [[nodiscard]] float Overcharge01() const noexcept;

            //! 満タンから数えた溜めすぎのフレーム数を返す
            //! 溜めすぎきった後も押している間は数え続ける。満タンまでと溜めすぎで出た後は 0。
            //! 離したフレームは控えた値から読む
            [[nodiscard]] int OverchargedSteps() const noexcept;

            //! 溜めすぎきってチャージを控え、突進が出るのを待っている場合 true、それ以外の場合は false
            [[nodiscard]] bool IsAwaitingLaunch() const noexcept;

            //! 突進が出た事を知らせる
            //! 出るのを待っている間だけ、放すまでの使い切りへ移す。それ以外は何もしない
            void MarkLaunched() noexcept;

            // 秒でなくフレーム数で持つのは、固定ステップの整数で数えると同じ入力列が必ず同じ判定になるため。
            // 自機が欄「チャージしきい値秒」「チャージ満タン秒」「溜めすぎの秒数」から、Step の前に毎回入れる
            int chargeThresholdSteps = 0;
            int chargeMaxSteps = 0;
            int overchargeSteps = 0; // 満タンから押したままで出るまで

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
    } // namespace Level
} // namespace GL

#pragma once

namespace NS
{
    class HitSensor;
    class HitSensorDirector;

    //! 所属シーンのセンサーの管理クラスを取得するインターフェース
    class IUseHitSensor
    {
    public:
        //! センサーの管理クラス。シーンに居ない間は nullptr
        [[nodiscard]] virtual HitSensorDirector* GetHitSensorDirector() const noexcept = 0;

    protected:
        ~IUseHitSensor() = default;
    };

    // センサーは、アクターを作る時 (OnInit) に部品を作る行の隣で InitHitSensor を 1 回呼んで入れ、
    // アクターが終わる時 (OnEndPlay) に EndHitSensor で外す。出る・消える時は EnableHitSensor・DisableHitSensor で
    // センサー自身の値を切り替え、入れたままにするので調べる順は変わらない

    //! @brief sensor を管理クラスへ入れる。入っていれば何もしない。管理クラスが無ければ何もしない
    //! @details 入れただけでは調べる対象にならない。EnableHitSensor で有効にする。sensor は EndHitSensor まで生きていること
    void InitHitSensor(const IUseHitSensor* user, HitSensor* sensor);
    //! sensor を管理クラスから外す。入っていなければ何もしないので、2 回呼んでもよい
    void EndHitSensor(const IUseHitSensor* user, HitSensor* sensor) noexcept;

    //! 持ち主が出た時に、sensor を調べる対象へ戻す。ゲームが Invalidate で外した物は外したまま
    void EnableHitSensor(HitSensor* sensor) noexcept;
    //! 持ち主が消えた時に、sensor を入れたまま調べる対象から外す
    void DisableHitSensor(HitSensor* sensor) noexcept;
} // namespace NS

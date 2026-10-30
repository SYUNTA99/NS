#pragma once

namespace NS
{
    class HitSensor;
    class HitSensorDirector;

    //! 所属シーンのセンサーの管理クラスを取得するインターフェース
    class IGetHitSensor
    {
    public:
        //! センサーの管理クラス。シーンに居ない間は nullptr
        [[nodiscard]] virtual HitSensorDirector* GetHitSensorDirector() const noexcept = 0;

    protected:
        ~IGetHitSensor() = default;
    };

    // センサーは、アクターのクラスが OnInit で HitSensorsObj の AddShape・AddFollow を呼んで作る
    // HitSensorsObj が作って InitHitSensor で 1 回入れ、アクターが終わる時 (OnEndPlay) に EndHitSensor で外す。
    // 出る・消える時は EnableHitSensor・DisableHitSensor でセンサー自身の値を切り替え、入れたままにするので調べる順は変わらない

    //! sensor を管理クラスへ入れる。入っていれば何もしない。管理クラスが無ければ何もしない
    //! 入れただけでは調べる対象にならない。EnableHitSensor で有効にする。sensor は EndHitSensor まで生きていること
    void InitHitSensor(const IGetHitSensor* user, HitSensor* sensor);
    //! sensor を管理クラスから外す。入っていなければ何もしないので、2 回呼んでもよい
    //! 重なっていた組の終わり (OnSensorExit) を、sensor の持ち主へこの場で届ける
    void EndHitSensor(const IGetHitSensor* user, HitSensor* sensor);

    //! 持ち主が出た時に、sensor を調べる対象へ戻す。ゲームが Invalidate で外した物は外したまま
    void EnableHitSensor(HitSensor* sensor) noexcept;
    //! 持ち主が消えた時に、sensor を入れたまま調べる対象から外す
    void DisableHitSensor(HitSensor* sensor) noexcept;
} // namespace NS

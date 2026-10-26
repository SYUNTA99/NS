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

    // センサーは、アクターのクラスが NS_SENSOR・NS_FOLLOW_SENSOR で宣言し、基底の SceneActor が OnInit の前に作って
    // InitHitSensor で 1 回入れ、アクターが終わる時 (OnEndPlay) に EndHitSensor で外す。
    // 出る・消える時は EnableHitSensor・DisableHitSensor でセンサー自身の値を切り替え、入れたままにするので調べる順は変わらない

    //! @brief sensor を管理クラスへ入れる。入っていれば何もしない。管理クラスが無ければ何もしない
    //! @details 入れただけでは調べる対象にならない。EnableHitSensor で有効にする。sensor は EndHitSensor まで生きていること
    void InitHitSensor(const IUseHitSensor* user, HitSensor* sensor);
    //! @brief sensor を管理クラスから外す。入っていなければ何もしないので、2 回呼んでもよい
    //! @details 重なっていた組の終わり (OnSensorExit) を、sensor の持ち主へこの場で届ける
    void EndHitSensor(const IUseHitSensor* user, HitSensor* sensor);

    //! 持ち主が出た時に、sensor を調べる対象へ戻す。ゲームが Invalidate で外した物は外したまま
    void EnableHitSensor(HitSensor* sensor) noexcept;
    //! 持ち主が消えた時に、sensor を入れたまま調べる対象から外す
    void DisableHitSensor(HitSensor* sensor) noexcept;
} // namespace NS

#pragma once

#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/System/SceneSystem.h"

#include <utility>
#include <vector>

namespace NS
{
    class SceneActor;

    //! 有効な異なる持ち主のセンサーを照合し、双方の AttackSensor を呼ぶ
    //! センサーの種類による判定は持ち主が行う
    class HitSensorDirector final : public SceneSystem
    {
    public:
        //! センサーを入れる。二重登録は無視する
        void Register(HitSensor* sensor);
        //! センサーを外す
        void Unregister(HitSensor* sensor) noexcept;

        //! 重なった組を集め、両方の持ち主の AttackSensor を呼ぶ。呼ぶ間に外れたか持ち主が消えたセンサーは飛ばす
        void OnTick() override;

        //! 世界座標の volume に重なる有効なセンサーを登録順に返す。参照は非所有
        //! ignore の持ち主を除き、種類は絞らない。ignore が nullptr なら除外なし
        [[nodiscard]] std::vector<HitSensor*> FindOverlaps(const SensorVolume& volume, const SceneActor* ignore) const;

        //! 登録されたセンサーの世界座標の形を、持ち主の根の世界行列で出す。sensor と持ち主は null でない事
        [[nodiscard]] static SensorVolume VolumeOf(const HitSensor* sensor) noexcept;

        //! 登録中のセンサー。登録順、非所有
        [[nodiscard]] const std::vector<HitSensor*>& Sensors() const noexcept { return m_sensors; }

    private:
        [[nodiscard]] bool IsRegistered(const HitSensor* sensor) const noexcept;
        // 入っていて、持ち主が出ているか。持ち主が消えてもセンサーは入ったままなので、呼ぶ途中で消えた物をこれで飛ばす。
        // 呼ぶ途中の Invalidate では組を飛ばさないので IsValid は読まない
        [[nodiscard]] bool IsPresent(const HitSensor* sensor) const noexcept;

        std::vector<HitSensor*> m_sensors; // 登録順、非所有
    };
} // namespace NS

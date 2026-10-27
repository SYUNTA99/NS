#pragma once

#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/System/SceneSystem.h"

#include <utility>
#include <vector>

namespace NS
{
    class SceneActor;

    /// 有効な異なる持ち主のセンサーの重なりを照合し、始まり・続き・終わりを持ち主へ知らせる
    /// センサーの種類による判定は持ち主が行う。前の固定フレームに知らせた組を覚えて、今の組と比べる
    class HitSensorDirector final : public SceneSystem
    {
    public:
        /// センサーを入れる。二重登録は無視する
        void Register(HitSensor* sensor);
        /// センサーを外す
        /// 始まりを知らせた組の終わりを、外したセンサーの持ち主へはこの場で、相手の持ち主へは次の OnTick で
        /// 知らせる。相手の持ち主へ渡す相手は nullptr で、外したセンサーの番地は外した後に読まない
        void Unregister(HitSensor* sensor);

        /// 重なった組を集め、前の固定フレームの組と比べて、両方の持ち主へ始まり・続き・終わりを知らせる
        /// 知らせる途中で外れたか持ち主が消えたセンサーには、始まりと続きを知らせない
        void OnTick() override;

        /// 世界座標の volume に重なる有効なセンサーを登録順に返す。参照は非所有
        /// ignore の持ち主を除き、種類は絞らない。ignore が nullptr なら除外なし
        [[nodiscard]] std::vector<HitSensor*> FindOverlaps(const SensorVolume& volume, const SceneActor* ignore) const;

        /// 登録されたセンサーの世界座標の形を、持ち主の根の世界行列で出す。sensor と持ち主は null でない事
        [[nodiscard]] static SensorVolume VolumeOf(const HitSensor* sensor) noexcept;

        /// 登録中のセンサー。登録順、非所有
        [[nodiscard]] const std::vector<HitSensor*>& Sensors() const noexcept { return m_sensors; }

    private:
        // 向きのある組。self が知らせる先の持ち主のセンサー、other が相手
        struct SensorOverlap
        {
            HitSensor* self = nullptr;
            HitSensor* other = nullptr;

            [[nodiscard]] bool operator==(const SensorOverlap&) const noexcept = default;
        };

        // 今の固定フレームに重なっている有効な組を、両方の向きで集める
        [[nodiscard]] std::vector<SensorOverlap> CollectOverlaps() const;
        // 前の組と今の組を比べ、終わり → 始まりと続きの順に知らせる
        void NotifyOverlaps(const std::vector<SensorOverlap>& current);
        // overlap.self の持ち主へ終わりを知らせる。overlap.self が外れていれば知らせない
        void NotifyExit(const SensorOverlap& overlap);

        [[nodiscard]] bool IsRegistered(const HitSensor* sensor) const noexcept;
        // 入っていて、持ち主が出ているか。持ち主が消えてもセンサーは入ったままなので、呼ぶ途中で消えた物をこれで飛ばす。
        // 呼ぶ途中の Invalidate では組を飛ばさないので IsValid は読まない
        [[nodiscard]] bool IsPresent(const HitSensor* sensor) const noexcept;

        std::vector<HitSensor*> m_sensors;          // 登録順、非所有
        std::vector<SensorOverlap> m_overlaps;      // 前の OnTick で始まりか続きを知らせた組
        std::vector<SensorOverlap> m_pendingExits;  // 相手が外れた組。other は nullptr で、次の OnTick で終わりを知らせる
    };
} // namespace NS

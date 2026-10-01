#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/ITickable.h"

#include <vector>

namespace NS::Obj
{
    class Actor;

    //! @brief シーンのヒットセンサーを束ね、1 フレームに 1 回重なりを調べる。オデッセイの HitSensorDirector に当たる
    //! @details 組み合わせの表に載った種類の組だけを調べ、重なった組の調べる側の持ち主の Actor::AttackSensor を呼ぶ
    //! 同じ Actor
    //! のセンサーどうしは調べない。重なりは物理エンジンを使わずに形どうしで直に解くので、物理の層と関係なく、 Scene
    //! が値で持ち、ObjectList の更新へ自分を登録する
    class HitSensorDirector final : public ITickable, public NS::Core::NonCopyable
    {
    public:
        //! センサーを入れる。二重登録は無視する
        void Register(HitSensor* sensor);
        //! センサーを外す
        void Unregister(HitSensor* sensor) noexcept;

        //! @brief attacker の種類のセンサーが target の種類のセンサーを調べるか。組み合わせの表
        //! @details 体当たりは物の体を、範囲はプレイヤーの体を調べる。それ以外の組は調べない
        [[nodiscard]] static bool Checks(HitSensorType attacker, HitSensorType target) noexcept;

        //! 重なった組を集め、調べる側の持ち主の AttackSensor を呼ぶ。呼ぶ間に外れたセンサーは飛ばす
        void OnTick() override;

        //! @brief volume に重なり、attackerType が調べる種類の有効なセンサーを登録順に返す
        //! @details 自分のセンサーを持たずに重なりを問う口。体当たりはこれで次の固定ステップの位置を先に調べる
        //! ignore の持ち主のセンサーは返さない
        [[nodiscard]] std::vector<HitSensor*> FindOverlaps(const SensorVolume& volume,
                                                           HitSensorType attackerType,
                                                           const Actor* ignore) const;

        //! 種類が type の有効なセンサーを登録順に fn へ渡す
        template <class Fn> void ForEachSensor(HitSensorType type, Fn&& fn) const
        {
            for (HitSensor* sensor : m_sensors)
            {
                if (sensor->Type() == type && sensor->IsValid())
                {
                    fn(*sensor);
                }
            }
        }

        //! 登録中のセンサー。登録順、非所有
        [[nodiscard]] const std::vector<HitSensor*>& Sensors() const noexcept { return m_sensors; }

    private:
        [[nodiscard]] bool IsRegistered(const HitSensor* sensor) const noexcept;

        std::vector<HitSensor*> m_sensors; // 登録順、非所有
        // OnTick の作業用。重なった組 (調べる側, 調べられる側)
        std::vector<std::pair<HitSensor*, HitSensor*>> m_pairs;
    };
} // namespace NS::Obj

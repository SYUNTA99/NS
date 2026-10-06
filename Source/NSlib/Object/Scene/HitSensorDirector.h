#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Object/Components/HitSensor.h"
#include "NSlib/Object/ITickable.h"

#include <utility>
#include <vector>

namespace NS::Obj
{
    class Actor;

    //! @brief シーンのヒットセンサーを束ね、1 フレームに 1 回重なりを調べる。オデッセイの HitSensorDirector に当たる
    //! @details 持ち主の違う有効なセンサーの組を全部調べ、重なった組は両方の持ち主の Actor::AttackSensor を
    //! 自分と相手を入れ替えて 1 回ずつ呼ぶ。種類は見ない。相手の種類を見て応じるかは受け手の持ち主が決める
    //! 重なりは物理エンジンを使わずに形どうしで直に解くので、物理の層と関係ない。
    //! Scene が値で持ち、ObjectList の更新へ自分を登録する
    class HitSensorDirector final : public ITickable, public NS::NonCopyable
    {
    public:
        //! センサーを入れる。二重登録は無視する
        void Register(HitSensor* sensor);
        //! センサーを外す
        void Unregister(HitSensor* sensor) noexcept;

        //! 重なった組を集め、両方の持ち主の AttackSensor を呼ぶ。呼ぶ間に外れたセンサーは飛ばす
        void OnTick() override;

        //! @brief volume に重なる有効なセンサーを登録順に返す。先読みの問いの口
        //! @details 自分のセンサーを持たずに、まだ動いていない形で重なりを問う。体当たりはこれで、この固定ステップで
        //! 進んだ先の形を身体が動く前に問う。絞るのは仕組みの条件だけで、有効か、ignore の持ち主でないかを見る。
        //! どの種類を相手にするかは問う側が決める
        //! @param[in] volume 問う形。世界座標
        //! @param[in] ignore このセンサーの持ち主は返さない。nullptr なら誰も除かない
        //! @return 重なった有効なセンサー。登録順、非所有
        [[nodiscard]] std::vector<HitSensor*> FindOverlaps(const SensorVolume& volume, const Actor* ignore) const;

        //! 登録中のセンサー。登録順、非所有
        [[nodiscard]] const std::vector<HitSensor*>& Sensors() const noexcept { return m_sensors; }

    private:
        [[nodiscard]] bool IsRegistered(const HitSensor* sensor) const noexcept;

        std::vector<HitSensor*> m_sensors; // 登録順、非所有
    };
} // namespace NS::Obj

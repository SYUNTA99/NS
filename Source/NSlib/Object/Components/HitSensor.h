#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Object/Component.h"

#include <cstdint>
#include <functional>

namespace NS::Phys
{
    struct Capsule;
}

namespace NS::Obj
{
    //! センサーの形
    enum class HitSensorShape : int
    {
        Sphere = 0,  //!< 球
        Capsule = 1, //!< 根の上の向きに伸びるカプセル
        Box = 2,     //!< 根の向きの箱
    };

    //! @brief 世界座標のセンサーの形。球とカプセルは線分と半径、箱は向きのある箱で持つ
    //! @details 重なりは物理エンジンを使わずにこの形同士で直に解く。順番に依らず、毎回同じ答になる
    struct SensorVolume
    {
        bool isBox = false;
        NS::Vector3 a{0.0f, 0.0f, 0.0f}; // 線分の片端。球は a と b が同じ
        NS::Vector3 b{0.0f, 0.0f, 0.0f}; // 線分のもう片端
        float radius = 0.0f;                   // 線分からの半径。箱は 0
        NS::OBB box{};                   // 箱の形。isBox の時だけ使う

        [[nodiscard]] static SensorVolume Sphere(const NS::Vector3& center, float radius) noexcept;
        [[nodiscard]] static SensorVolume Capsule(const NS::Phys::Capsule& capsule) noexcept;
        [[nodiscard]] static SensorVolume Box(const NS::OBB& box) noexcept;

        //! 形を包む軸並行の箱
        [[nodiscard]] NS::AABB Bounds() const noexcept;
        //! 形の中心
        [[nodiscard]] NS::Vector3 Center() const noexcept;
    };

    //! 2 つの形が重なるか。触れているだけも重なりに数える
    [[nodiscard]] bool VolumesOverlap(const SensorVolume& lhs, const SensorVolume& rhs) noexcept;

    //! @brief Actor に付く当たりの調べ役の基底。オデッセイの HitSensor に当たる
    //! @details 種類と、シーンの HitSensorDirector へ OnStart で入り OnEndPlay で出る登録だけを持つ
    //! 調べ役は 1 フレームに 1 回、センサーの段で持ち主の違うセンサーの重なりを調べ、重なった組の両方の持ち主の
    //! Actor::AttackSensor を呼ぶ。相手の種類を見て応じるかは持ち主が決める
    //! 形は派生が決める。欄は持たず、派生ごとに持つ (Collision の一族と同じ形)
    //! 抽象基底なので TypeRegistry には登録しない
    class HitSensor : public Component
    {
    public:
        HitSensor() noexcept;

        //! シーンの調べ役へ入る
        void OnAppear() override { HitSensor::OnStart(); }
        void OnKill() noexcept override { HitSensor::OnEndPlay(); }
        void OnStart() override;
        //! シーンの調べ役から出る
        void OnEndPlay() override;

        //! @brief 種類を書く
        //! @details 値に名前は付けない。名前と、どの種類に応じるかは Game の SensorKinds.h が持つ
        //! @param[in] kind 種類の値。0 は未設定
        void SetKind(std::uint8_t kind) noexcept { m_kind = kind; }
        //! 種類の値。0 は未設定
        [[nodiscard]] std::uint8_t Kind() const noexcept { return m_kind; }

        //! 調べる対象か。部品が効いていて、無効にされていない時だけ真
        [[nodiscard]] bool IsValid() const noexcept { return m_valid && IsActive(); }
        //! 調べる対象へ戻す
        void Validate() noexcept { m_valid = true; }
        //! 調べる対象から外す。飛んでいる間だけ当たらない物などに使う
        void Invalidate() noexcept { m_valid = false; }

        //! 世界座標の形。調べ役と問う側が毎回これを読む
        [[nodiscard]] virtual SensorVolume WorldVolume() const noexcept = 0;

        NS_REFLECT_NONE(HitSensor, Component)

    private:
        std::uint8_t m_kind = 0;   // 種類の値。0 は未設定で、どの受け手も応じない
        bool m_valid = true;       // 調べる対象か
        bool m_registered = false; // 調べ役へ入っているか
    };

    //! @brief 形を自分で持つ調べ役。ゴールと落下死の範囲と、2 つ目のセンサーの枠 AttackSensor が使う
    //! @details 形 (球・カプセル・箱) と大きさを欄に持ち、大きさは根の世界のスケールに付いて来る
    //! 種類と形はクラスがコンストラクタで決め、大きさは値で調整する
    class ShapeHitSensor final : public HitSensor
    {
    public:
        ShapeHitSensor() noexcept;

        //! 球にする。半径は根のスケール前
        void SetSphere(float radius) noexcept;
        //! 根の上の向きに伸びるカプセルにする。halfHeight は中心から端の半球の中心まで
        void SetCapsule(float radius, float halfHeight) noexcept;
        //! 根の向きの箱にする。halfExtents は中心から各面まで
        void SetBox(const NS::Vector3& halfExtents) noexcept;

        [[nodiscard]] HitSensorShape Shape() const noexcept { return m_shape; }
        [[nodiscard]] float Radius() const noexcept { return m_radius; }
        [[nodiscard]] float HalfHeight() const noexcept { return m_halfHeight; }
        [[nodiscard]] const NS::Vector3& BoxHalfExtents() const noexcept { return m_boxHalfExtents; }

        //! 根からの中心のずれ (根のローカル)
        void SetCenterOffset(const NS::Vector3& offset) noexcept { m_centerOffset = offset; }
        [[nodiscard]] const NS::Vector3& CenterOffset() const noexcept { return m_centerOffset; }

        //! 世界座標の形。大きさに根の世界のスケールを掛ける
        [[nodiscard]] SensorVolume WorldVolume() const noexcept override;

        // 形の大きさは種類の既定値と個体の上書きで調整できる。種類と形はクラスが決めるので出さない
        NS_REFLECT_BEGIN(ShapeHitSensor, HitSensor)
        NS_REFLECT_FIELD(m_radius, "半径")
        NS_REFLECT_FIELD(m_halfHeight, "半分の高さ")
        NS_REFLECT_FIELD(m_boxHalfExtents, "箱の半径")
        NS_REFLECT_FIELD(m_centerOffset, "中心オフセット")
        NS_REFLECT_END()

    private:
        HitSensorShape m_shape = HitSensorShape::Sphere;
        float m_radius = 0.5f;                                // 球とカプセルの半径 (スケール前)
        float m_halfHeight = 0.5f;                            // カプセルの中心から端の半球の中心まで (スケール前)
        NS::Vector3 m_boxHalfExtents{0.5f, 0.5f, 0.5f}; // 箱の中心から各面まで (スケール前)
        NS::Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};   // 根からの中心のずれ
    };

    //! @brief 持ち主の体の形をそのまま映す調べ役。自機と置物の体のセンサーが使う
    //! @details 形の正は持ち主の当たりの部品 (Collider・SphereCollision) で、ここは毎回それを読むだけ。
    //! 写しを持たないので、寸法の欄を変えたその場で範囲の照合とエディタの線に効く。欄は持たない
    //! 関数は持ち主のクラスがコンストラクタで渡す。既定コンストラクタが無いので TypeRegistry には登録しない
    class FollowHitSensor final : public HitSensor
    {
    public:
        //! @brief 形を答える関数を受けて作る
        //! @param[in] volume 世界座標の形を返す関数。事前条件: 空でない。空なら WorldVolume は根に長さ 0 の球を返し
        //! NS_LOG_ERROR で知らせる
        explicit FollowHitSensor(std::function<SensorVolume()> volume) noexcept;

        //! 世界座標の形。受けた関数の答えをそのまま返す
        [[nodiscard]] SensorVolume WorldVolume() const noexcept override;

        NS_REFLECT_NONE(FollowHitSensor, HitSensor)

    private:
        std::function<SensorVolume()> m_volume; // 持ち主の当たりの形を世界座標で返す関数
    };
} // namespace NS::Obj

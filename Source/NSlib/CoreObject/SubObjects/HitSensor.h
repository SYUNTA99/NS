#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/SubObject.h"

#include "NSlib/Core/Types.h"
#include <functional>

namespace NS
{
    namespace Phys
    {
        struct Capsule;
    }
} // namespace NS

namespace NS
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
        Vector3 a{0.0f, 0.0f, 0.0f}; // 線分の片端。球は a と b が同じ
        Vector3 b{0.0f, 0.0f, 0.0f}; // 線分のもう片端
        float radius = 0.0f;             // 線分からの半径。箱は 0
        OBB box{};                   // 箱の形。isBox の時だけ使う

        [[nodiscard]] static SensorVolume Sphere(const Vector3& center, float radius) noexcept;
        [[nodiscard]] static SensorVolume Capsule(const Phys::Capsule& capsule) noexcept;
        [[nodiscard]] static SensorVolume Box(const OBB& box) noexcept;

        //! 形を包む軸並行の箱
        [[nodiscard]] AABB Bounds() const noexcept;
        //! 形の中心
        [[nodiscard]] Vector3 Center() const noexcept;
    };

    //! 2 つの形が重なるか。触れているだけも重なりに数える
    [[nodiscard]] bool VolumesOverlap(const SensorVolume& lhs, const SensorVolume& rhs) noexcept;

    //! シーンへ登録するセンサーの基底。形状は派生、種類への応答は持ち主が決める
    class HitSensor : public SubObject
    {
    public:
        HitSensor() noexcept = default;

        //! シーンの調べ役へ入る。入っていれば何もしない
        void Register();
        //! シーンの調べ役から出る
        void Unregister();

        //! 0 は未設定。種類の意味はゲーム側が定義する
        void SetKind(uint8 kind) noexcept { m_kind = kind; }
        //! 種類の値。0 は未設定
        [[nodiscard]] uint8 Kind() const noexcept { return m_kind; }

        //! 調べる対象か。部品が効いていて、無効にされていない時だけ真
        [[nodiscard]] bool IsValid() const noexcept { return m_valid && IsOwnerAppeared(); }
        //! 調べる対象へ戻す
        void Validate() noexcept { m_valid = true; }
        //! 調べる対象から外す。飛んでいる間だけ当たらない物などに使う
        void Invalidate() noexcept { m_valid = false; }

        //! 世界座標の形。調べ役と問う側が毎回これを読む
        [[nodiscard]] virtual SensorVolume WorldVolume() const noexcept = 0;

        NS_REFLECT_NONE(HitSensor, SubObject)

    private:
        uint8 m_kind = 0;      // 種類の値。0 は未設定で、どの受け手も応じない
        bool m_valid = true;       // 調べる対象か
        bool m_registered = false; // 調べ役へ入っているか
    };

    //! 独立した形状を持つセンサー。寸法は持ち主の世界スケールに従う
    class ShapeHitSensor final : public HitSensor
    {
    public:
        ShapeHitSensor() noexcept = default;

        //! 球にする。半径は根のスケール前
        void SetSphere(float radius) noexcept;
        //! 根の上の向きに伸びるカプセルにする。halfHeight は中心から端の半球の中心まで
        void SetCapsule(float radius, float halfHeight) noexcept;
        //! 根の向きの箱にする。halfExtents は中心から各面まで
        void SetBox(const Vector3& halfExtents) noexcept;

        [[nodiscard]] HitSensorShape Shape() const noexcept { return m_shape; }
        [[nodiscard]] float Radius() const noexcept { return m_radius; }
        [[nodiscard]] float HalfHeight() const noexcept { return m_halfHeight; }
        [[nodiscard]] const Vector3& BoxHalfExtents() const noexcept { return m_boxHalfExtents; }

        //! 根からの中心のずれ (根のローカル)
        void SetCenterOffset(const Vector3& offset) noexcept { m_centerOffset = offset; }
        [[nodiscard]] const Vector3& CenterOffset() const noexcept { return m_centerOffset; }

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
        float m_radius = 0.5f;                          // 球とカプセルの半径 (スケール前)
        float m_halfHeight = 0.5f;                      // カプセルの中心から端の半球の中心まで (スケール前)
        Vector3 m_boxHalfExtents{0.5f, 0.5f, 0.5f}; // 箱の中心から各面まで (スケール前)
        Vector3 m_centerOffset{0.0f, 0.0f, 0.0f};   // 根からの中心のずれ
    };

    //! 持ち主の衝突形状を毎回読むセンサー。形状の写しや編集用の寸法は持たない
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
} // namespace NS

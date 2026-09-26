#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <cstdint>

namespace NS::Obj
{
    class RigidBody;
}

namespace NS::Game::Level
{
    //! @brief 飛ばした物の曲線の形。ImpactResolver が組み、LaunchedBody が飛んでいる間持つ
    //! @details PlayerComponent::ReboundVelocityFor も自機の反動の初速をこの形の式で出す
    //! 上りは riseGravity で減速する。既定は世界の重力の既定値 NS::Phys::k_DefaultGravityY の大きさで、
    //! 実行中に世界の重力を変えても曲線は追わない
    //! 下りの重力は上りの重力の fallGravityScale 倍
    //! 縦の速さの大きさが頂点の帯の縦速度より小さい間は、その時の重力にさらに帯の重力倍率を掛ける
    //! 水平は一定の速さで進み、発射の高さへ戻った所で飛ぶ距離だけ進んでいる
    struct LaunchArc
    {
        NS::Core::Vector3 direction{1.0f, 0.0f, 0.0f}; // 飛ぶ向き。水平の成分だけを使う
        float distance = 0.0f;                         // 発射から発射の高さへ戻るまでの水平の距離 (m)
        float apexHeight = 0.0f;                       // 発射の高さから頂点までの高さ (m)
        // 上りの重力の大きさ (m/s²)
        float riseGravity = -NS::Phys::k_DefaultGravityY;
        float fallGravityScale = 1.0f;     // 下りの重力 ÷ 上りの重力
        float apexBandSpeed = 0.0f;        // 頂点の帯の縦速度 (m/s)。0 なら帯は無い
        float apexBandGravityScale = 1.0f; // 頂点の帯の間に重力へ掛ける倍率
    };

    //! @brief 曲線の発射の瞬間の速度 (m/s) を返す
    //! @details 水平は飛ぶ距離 ÷ 発射の高さへ戻るまでの秒、上向きは頂点の高さへちょうど届く速さ
    //! 距離・高さ・上りの重力・2 つの倍率が有限の正でない時、帯の縦速度が有限の 0 以上でない時、
    //! 向きに水平の成分が無い時は 0 を返す
    [[nodiscard]] NS::Core::Vector3 LaunchArcInitialVelocity(const LaunchArc& arc) noexcept;

    //! 飛ばした物の段階
    enum class LaunchPhase : std::uint8_t
    {
        Resting = 0, // 置かれている、または止まった。止まった時は RigidBody をキネマティックへ戻す
        Arc = 1,     // 曲線で飛んでいる
        Rigid = 2,   // 剛体の物理に任せて飛んでいる・転がっている
    };

    //! @brief 押し飛ばされて転がり、止まるか壊れるまでを受け持つ Component
    //! @details body は自分で作らず、同じ配置物の RigidBody を動かす
    //! 置かれている間と止まった後は RigidBody をキネマティックにし、飛んでいる間だけダイナミックにする
    //! 曲線の間は RigidBody の重力と減衰を切り、曲線の次の 1 フレームの変位 ÷ dt を速度として毎フレーム書く
    //! 回る速さも同じフレームに、回転の強さ × 水平の速さの前転へ書き直す
    //! 近づく向きの接触が出たフレームに書くのをやめて欄を戻し、剛体の物理に任せる
    //! 質量・摩擦・跳ね返りは RigidBody が持ち、ここは回転の強さ・止まってから消えるまで・破片の設定だけを持つ
    //! RigidBody が無い配置物は、飛ばす時にキネマティックの RigidBody を足してから飛ばす
    //! 依存: NS::Obj::RigidBody, NS::Obj::Collider, Breakable
    class LaunchedBody : public NS::Obj::Component
    {
    public:
        // 物理の更新が済んでから body を読むので LateUpdate 帯で名乗る
        LaunchedBody() noexcept;

        //! @brief 根の今の位置から arc の曲線で飛ばす
        //! @details body を根の姿勢へ置き直し、RigidBody の「重力を使う」を偽、「移動の減衰」「回転の減衰」を 0
        //! にしてからダイナミックへ切り替え、曲線の最初の 1 フレームの変位 ÷ dt を速度として置く。前転の角速度も入れる
        //! 飛んでいる最中に呼ぶと、今の位置から曲線を引き直す
        //! LaunchArcInitialVelocity が 0 を返す arc は無視する。RigidBody が無ければ足す
        //! body の生成で確保が起きるので noexcept にしない
        void Launch(const LaunchArc& arc);

        //! @brief 同じ配置物の RigidBody をダイナミックにして velocity で飛ばし、はじめから剛体の物理に任せる
        //! @details 前転の角速度も入れる。非有限の velocity は無視する。RigidBody が無ければ足す
        //! body の生成で確保が起きるので noexcept にしない
        void LaunchRigid(const NS::Core::Vector3& velocity);

        //! @brief 根を position・rotation へ置き直し、置かれた物へ戻す
        //! @details body も同じ所へ瞬間移動する。曲線の間なら切った「重力を使う」と減衰を切る前の値へ戻し、
        //! 速度と角速度を 0 にしてキネマティックへ戻す。飛んでいない物も同じ所へ置き直す
        //! RigidBody が無ければ根だけを置き直す。持ち主が居なければ何もしない
        //! @param position 根の位置。親がいれば親から見た値で、場面の JSON の位置と同じ
        //! @param rotation 根の回転。親がいれば親から見た値
        void ResetTo(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation);

        //! 今の段階を返す
        [[nodiscard]] LaunchPhase Phase() const noexcept { return m_phase; }

        //! 段階が Arc か Rigid の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsFlying() const noexcept { return m_phase != LaunchPhase::Resting; }

        //! 線速度 (m/s) を返す。曲線の間は直近の物理が使った速度、剛体の間は body の速度、置かれている間は 0
        [[nodiscard]] NS::Core::Vector3 Velocity() const;

        void SetRestLifeSeconds(float seconds) noexcept;

        void SetDebrisCount(int count) noexcept;

        // ObjectList からは消さない。更新の最中に消すと集めた並びに解放済みのポインタが残る
        void Shatter();

        void OnUpdate() override;

        void OnEndPlay() override;

        NS_REFLECT_BEGIN(LaunchedBody, NS::Obj::Component)
        NS_REFLECT_FIELD(m_spinPerSpeed, "回転の強さ")
        NS_REFLECT_FIELD(m_restLifeSeconds, "止まってから消える秒")
        NS_REFLECT_FIELD(m_debrisCount, "破片の数")
        NS_REFLECT_FIELD(m_debrisSpeed, "破片の速さ")
        NS_REFLECT_FIELD(m_debrisLifeSeconds, "破片の寿命秒")
        NS_REFLECT_FIELD(m_debrisScale, "破片の大きさ")
        NS_REFLECT_FIELD(m_debrisBaseColor, "破片の色")
        NS_REFLECT_END()

    private:
        [[nodiscard]] NS::Core::Vector3 TumbleFrom(const NS::Core::Vector3& velocity) const noexcept;

        // 同じ配置物の RigidBody。無ければキネマティックで足し、collider を形として取り込ませて body を作る
        // 持ち主が Scene に居なければ null
        [[nodiscard]] NS::Obj::RigidBody* EnsureRigidBody();

        // 曲線の次の 1 フレームの変位 ÷ dt を body の速度に、前転の角速度を回る速さに書き、進めたフレーム数を 1 増やす
        void WriteArcVelocity(NS::Obj::RigidBody& rigidBody, float dt);

        // 曲線の間に切った RigidBody の欄を、切る前の値へ戻す。body へ入るのは次の PrePhysicsStep
        void RestoreArcFields(NS::Obj::RigidBody& rigidBody) noexcept;

        // 止まった所でキネマティックへ戻す。次に飛ばされるまでその場の当たりとして残る
        void ComeToRest();

        // 当たりごと消す。RigidBody と collider の body を外し、描画と更新も止める
        void HideAndSleep();

        float m_spinPerSpeed = 0.2f; // 水平の速さ 1 m/s あたりの前転の角速度 (ラジアン/秒)
        float m_restLifeSeconds = 0.0f;
        float m_restAge = 0.0f;
        int m_debrisCount = 5;      // 壊れた時に出す破片の数
        float m_debrisSpeed = 6.0f; // 質量 1 の物が壊れた時の破片の水平初速
        // 破片が止まってから消えるまでの秒。押し飛ばした配置物と違い破片は残さない
        float m_debrisLifeSeconds = 8.0f;
        // 破片 1 個のスケール。壊れた物より明確に小さくして、数で壊れた量を見せる
        float m_debrisScale = 0.25f;
        NS::Core::Vector3 m_debrisBaseColor{0.35f, 0.32f, 0.30f};

        LaunchPhase m_phase = LaunchPhase::Resting;
        LaunchArc m_arc{};                                     // 曲線の間に辿る曲線
        int m_arcFrames = 0;                                   // 曲線の起点から速度を書いたフレーム数
        NS::Core::Vector3 m_arcVelocity{0.0f, 0.0f, 0.0f};     // 最後に書いた曲線の速度 (m/s)。次の物理が使う
        NS::Core::Vector3 m_arcVelocityUsed{0.0f, 0.0f, 0.0f}; // 直近の物理が使った曲線の速度 (m/s)
        bool m_savedUseGravity = true;                         // 曲線の前の「重力を使う」
        float m_savedLinearDamping = 0.0f;                     // 曲線の前の「移動の減衰」
        float m_savedAngularDamping = 0.0f;                    // 曲線の前の「回転の減衰」
    };
} // namespace NS::Game::Level

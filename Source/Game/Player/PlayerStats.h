#pragma once

namespace NS::Game::Player
{
    //! @brief 自機の調整値
    //! @details 持つのは素の値だけで、Inspector への露出も値の検査も PlayerComponent の側にある
    //! 走行の最高速度は入れない。MomentumComponent が速度状態に応じて毎歩書き換える値で、
    //! 手で決める調整値ではない
    struct PlayerStats
    {
        float jumpImpulse = 12.0f;     // ジャンプ初速
        float gravityUp = -25.0f;      // 上昇中の重力
        float gravityDown = -35.0f;    // 下降中の重力、上昇より強い
        float apexHangVy = 1.0f;       // 頂点とみなす縦速度のしきい値
        float apexHangScale = 0.5f;    // 頂点付近で重力に掛ける倍率
        float jumpReleaseScale = 0.6f; // 上昇中に離した時の縦速度倍率
        // 接地を離れてもジャンプを受ける猶予秒。実機プレイで詰めた約 1.5 フレームで、踏み外し直後のごく短い救済だけ残す
        float coyoteTime = 0.025f;
        // 着地前の先行ジャンプ入力を覚える秒。体当たりの先行入力と共用で、分けない
        // TODO: 暫定値。人の早押し誤差は概ね 100ms なので目標は 0.1 秒、体感で詰める
        float jumpBufferTime = 0.25f;
        float walkSpeed = 4.0f;     // 歩き速度
        float accelTau = 0.10f;     // 加速の時定数
        float decelTau = 0.10f;     // 減速の時定数
        float stickDeadzone = 0.3f; // スティック入力のデッドゾーン

        // どれも触って決める仮値
        float bodySlamSpeed = 20.0f;
        float bodySlamDistance = 10.0f;
        float tapSlamSpeed = 10.0f;
        float tapSlamUpSpeed = 3.0f;
        // 2.5 では目の前の物にしか届かず、狙って押す価値が無かった。実機で 2.5 倍にして詰める
        // 初速はそのままなので踏み込みは 0.25 秒から 0.625 秒へ延びる
        float tapSlamDistance = 6.25f;
        // 押した歩に控えた狙いをそのまま使う秒と、今の向きへ戻し切る秒。押してから離すまでにカメラが振れると
        // 飛ぶ先がずれる。離すまでの遅れの平均 0.15 秒はこの 2 つの間に入る。外の実測をそのまま借りた出発点で、
        // NS では測っていない
        float slamAimHoldTime = 0.11f;
        float slamAimFadeTime = 0.19f;
    };
} // namespace NS::Game::Player

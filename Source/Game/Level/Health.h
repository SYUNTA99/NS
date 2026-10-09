#pragma once

namespace GL::Level
{
    //! @brief プレイヤーの命。残量の所有と増減の能力だけ持ち、誰に削られるかは知らない
    //! @details 命を動かすのは持ち主の Player だけで、Player::Die が Deplete を、Player::ApplyDamage が ApplyDamage を呼ぶ
    //! Player の値メンバーで、進行役は Player::IsDead を読む
    class Health
    {
    public:
        Health() noexcept = default;
        //! @brief 最大の命を差し替え、残量が上回っていれば最大まで削る
        //! @param[in] value 新しい最大。0 以下なら何も変えない
        void SetMaxHealth(int value) noexcept;

        //! amount だけ削る。下限 0。既に 0 か amount が 0 以下なら何もしない
        void ApplyDamage(int amount) noexcept;

        //! 残量を 0 にする。死の知らせを受けた Player::Die が呼ぶ
        void Deplete() noexcept { m_current = 0; }

        //! 満タンへ戻す。プレイ突入とリスタートで呼ぶ
        void Reset() noexcept { m_current = m_maxHealth; }

        //! 残量が 0 以下の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsDead() const noexcept { return m_current <= 0; }
        [[nodiscard]] int Current() const noexcept { return m_current; }

    private:
        int m_maxHealth = 8; // 8 段階
        int m_current = 8;   // プレイ中の残量。保存せず、プレイ突入とリスタートで満タンに戻る
    };
} // namespace GL::Level

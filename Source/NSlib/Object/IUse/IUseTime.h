#pragma once

namespace NS::Obj
{
    //! @brief 体の時計。体の位置・回転・見た目を進める 1 歩の秒を返す
    //! @details Actor が持ち、部品は持ち主の Actor から引く
    //! 全体の止めと遅くはシーンが歩を間引くので、この値には入らない
    class IUseTime
    {
    public:
        //! 体の 1 歩の秒。固定の 1 歩の秒に体の倍率を掛けた値
        [[nodiscard]] virtual float BodyDelta() const noexcept = 0;
        [[nodiscard]] virtual float BodyTimeScale() const noexcept = 0;

    protected:
        ~IUseTime() = default;
    };
} // namespace NS::Obj

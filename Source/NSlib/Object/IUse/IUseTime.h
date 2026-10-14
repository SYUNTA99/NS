#pragma once

namespace NS::Obj
{
    //! 個体の身体と見た目の経過秒。シーン全体の停止・減速は含めない
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

#pragma once

namespace NS::Obj
{
    class Actor;
    class HitSensor;

    //! @brief Actor から Actor へ送る知らせの共通の基底
    //! @details 中身の無い基底から型ごとに派生し、種類は型そのもので表す。大半の知らせは種類だけで中身を持たない
    //! 受け手は Actor::ReceiveMsg で種類を調べ (IsMsg / MsgCast)、応じたかを真偽値で返す。どう応じるかは受け手が決める
    //! 型ごとに「送る」関数と「調べる」関数を対で用意する。オデッセイの SensorMsg に当たる
    //! 型の見分けは NS_MESSAGE が作る型ごとの印で行う。RTTI は使わない
    class Message
    {
    public:
        virtual ~Message() noexcept = default;

        //! 型ごとの印。NS_MESSAGE が実装する
        [[nodiscard]] virtual const void* Kind() const noexcept = 0;
    };

    //! msg が型 T か
    template <class T> [[nodiscard]] bool IsMsg(const Message& msg) noexcept
    {
        return msg.Kind() == T::StaticKind();
    }

    //! msg が型 T ならその中身、違えば nullptr
    template <class T> [[nodiscard]] const T* MsgCast(const Message& msg) noexcept
    {
        return IsMsg<T>(msg) ? static_cast<const T*>(&msg) : nullptr;
    }

    //! @brief センサーからセンサーへ知らせを送る。受け手は receiver の持ち主の ReceiveMsg
    //! @details 自分宛ては送らない。receiver が無効か持ち主が無ければ送らない
    //! @return 受け手が応じた場合 true
    bool SendMsg(const Message& msg, HitSensor& receiver, HitSensor* sender);

    //! @brief センサーを介さずに Actor へ直接知らせを送る。進行役からの一斉の知らせなど、重なりと関係の無い知らせに使う
    //! @return 受け手が応じた場合 true
    bool SendMsgToActor(const Message& msg, Actor& receiver);
} // namespace NS::Obj

//! 知らせの型の中に 1 度だけ書く。型ごとの印を作る
#define NS_MESSAGE(Type)                                                                                               \
public:                                                                                                                \
    [[nodiscard]] static const void* StaticKind() noexcept                                                             \
    {                                                                                                                  \
        static const char kind = 0;                                                                                    \
        return &kind;                                                                                                  \
    }                                                                                                                  \
    [[nodiscard]] const void* Kind() const noexcept override                                                           \
    {                                                                                                                  \
        return StaticKind();                                                                                           \
    }

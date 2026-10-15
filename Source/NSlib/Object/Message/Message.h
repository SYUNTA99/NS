#pragma once

namespace NS::Obj
{
    class SceneActor;
    class HitSensor;

    //! 個体間のメッセージ。NS_MESSAGE の識別値で種類を判定し、受け手が応答を決める
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
        if (!IsMsg<T>(msg))
        {
            return nullptr;
        }
        return static_cast<const T*>(&msg);
    }

    //! @brief センサーからセンサーへ知らせを送る。受け手は receiver の持ち主の ReceiveMsg
    //! @details 自分宛ては送らない。receiver が無効か持ち主が無ければ送らない
    //! @return 受け手が応じた場合 true
    bool SendMsg(const Message& msg, HitSensor& receiver, HitSensor* sender);

    //! @brief センサーを介さずに SceneActor へ直接知らせを送る。進行役からの一斉の知らせなど、重なりと関係の無い知らせに使う
    //! @return 受け手が応じた場合 true
    bool SendMsgToActor(const Message& msg, SceneActor& receiver);
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

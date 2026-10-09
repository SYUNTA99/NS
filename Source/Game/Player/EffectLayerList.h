#pragma once

#include "NSlib/Graphics/EffectScene.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Obj
{
    class SubObject;
}

namespace GL::Player
{
    //! @brief 自機のエフェクトの部品が出すと決めた層 1 つの記録
    //! @details フレームは持ち主の EffectLayerList::Step の数え方で、部品の OnUpdate 1 回が 1 フレーム
    struct EffectLayerRecord
    {
        std::uint32_t id = 0;            //!< 記録の番号。1 から振り、同じ EffectLayerList の中で使い回さない
        std::string name;                //!< 層の名前。charge.curl・impact.core のように組の頭から書く
        int startStep = 0;               //!< 出したフレーム
        std::optional<int> rootStopStep; //!< 親を止めたフレーム。止めていなければ空
        //! 消えたフレーム。残っていれば空。Stop で消したフレームか、Effekseer から消えた更新のあったフレーム
        std::optional<int> endStep;
        NS::Gfx::EffectHandle handle{}; //!< 再生したエフェクト。描画の無い世界と、絵を読めなかった層では無効
        //! 層の大きさか量。何を入れるかは層ごとに出した部品が決める。入れていなければ空
        std::optional<float> amount;
        //! 最後に置いた根の向き。置いていなければ空
        std::optional<NS::Quaternion> rotation;
    };

    //! @brief 自機のエフェクトの部品が出すと決めた層を、出した順に持つ記録
    //! @details 層を出す・親を止める・消すはここを通し、再生と記録を 1 か所で揃える。
    //! 描画の無い世界 (EffectScene が null) では再生せず、記録だけ残す。試しはこの記録を読む。
    //! 寿命で消えたかは、描画のある世界で BeginStep が Effekseer に問い合わせて書く。
    //! 描画の無い世界の層は Stop を呼ぶまで残っている扱い
    class EffectLayerList
    {
    public:
        //! 消えた層の記録を残すフレーム数。これを過ぎた記録は BeginStep が捨てる
        //! @details 押してから反動の着地までを試しと調べ物で振り返れる長さ。charge_and_hit で押した 20 から
        //! 着地 159 までの 140 フレームの 4 倍を取った
        explicit EffectLayerList(int keepEndedSteps = 600) noexcept : m_keepEndedSteps(keepEndedSteps) {}

        //! @brief フレームを 1 つ進め、前のフレームの更新で Effekseer から消えた層に消えたフレームを書く
        //! @details 保存フレーム数を過ぎた記録を捨て、StopAfter で決めたフレームが来た層を消す
        //! @param[in,out] effects 持ち主の世界の EffectScene。null なら寿命を見ず、記録だけ書く
        void BeginStep(NS::Gfx::EffectScene* effects);

        //! 今のフレームの番号。BeginStep を呼ぶ前は 0
        [[nodiscard]] int Step() const noexcept { return m_step; }

        //! @brief 層を 1 つ出し、今のフレームに出した記録を残す
        //! @details 絵は持ち主の部品が先に EffectScene::Preload しておく。読めていない名前は再生されず、
        //! 記録は無効なハンドルで残る
        //! @param[in,out] effects 再生先。null なら再生せずに記録だけ残す
        //! @param[in] name 層の名前。EffectScene::Preload に渡した名前と同じ綴り
        //! @param[in] desc 再生の姿勢・色・動的入力
        //! @return 記録の番号。StopRoot・Stop・Find に渡す
        [[nodiscard]] std::uint32_t Play(NS::Gfx::EffectScene* effects,
                                         std::string_view name,
                                         const NS::Gfx::EffectPlayDesc& desc);

        //! @brief 層の親を止める。出ていた子は寿命まで残る
        //! @details 無い番号、親を止めた後、消えた後なら何もしない
        //! @param[in,out] effects 再生先。null なら記録だけ書く
        //! @param[in] id Play が返した番号
        void StopRoot(NS::Gfx::EffectScene* effects, std::uint32_t id) noexcept;

        //! @brief 層を子も含めて消し、消えたフレームを今のフレームにする
        //! @details 無い番号、消えた後なら何もしない
        //! @param[in,out] effects 再生先。null なら記録だけ書く
        //! @param[in] id Play が返した番号
        void Stop(NS::Gfx::EffectScene* effects, std::uint32_t id) noexcept;

        //! @brief 層を今から steps フレーム後の BeginStep で、子も含めて消す
        //! @details steps が 0 以下なら次の BeginStep で消す。その時に消えた後なら何もしない
        //! @param[in] id Play が返した番号
        void StopAfter(std::uint32_t id, int steps);

        //! @brief 層の大きさか量を記録に書く
        //! @details 無い番号なら何もしない。2 回書くと後の値が残る
        //! @param[in] id Play が返した番号
        //! @param[in] amount 層の大きさか量
        void SetAmount(std::uint32_t id, float amount) noexcept;
        //! @brief 層の根を置いた向きを記録に書く
        //! @details 無い番号なら何もしない。描画の無い世界でも書く
        //! @param[in] id Play が返した番号
        //! @param[in] rotation 根の向き
        void SetRotation(std::uint32_t id, const NS::Quaternion& rotation) noexcept;

        //! @brief 番号の記録を返す
        //! @param[in] id Play が返した番号
        //! @return 記録。無い番号か、捨てた後なら nullptr
        [[nodiscard]] const EffectLayerRecord* Find(std::uint32_t id) const noexcept;

        //! 出した順に並んだ記録
        [[nodiscard]] const std::vector<EffectLayerRecord>& Records() const noexcept { return m_records; }

        //! @brief 今のフレームに出した層の名前を、出した順に out の末尾へ足す
        //! @param[out] out 名前を足す先。中身は消さない
        void AppendStartedNames(std::vector<std::string>& out) const;

        //! @brief 今のフレームに出した層の大きさか量を、AppendStartedNames と同じ順に out の末尾へ足す
        //! @param[out] out 大きさか量を足す先。入れていない層は空を足す。中身は消さない
        void AppendStartedAmounts(std::vector<std::optional<float>>& out) const;

        //! @brief 残っている層の名前を、出した順に out の末尾へ足す
        //! @details 同じ名前の層が 2 つ残っていれば 2 回足す。親を止めた層は子が残る間だけ残っている
        //! @param[in] effects 持ち主の世界の EffectScene。null なら Stop していない層を全部残っているとする
        //! @param[out] out 名前を足す先。中身は消さない
        void AppendLiveNames(const NS::Gfx::EffectScene* effects, std::vector<std::string>& out) const;

    private:
        int m_keepEndedSteps;
        [[nodiscard]] EffectLayerRecord* FindMutable(std::uint32_t id) noexcept;

        struct ScheduledStop
        {
            std::uint32_t id = 0;
            int step = 0;
        };

        std::vector<EffectLayerRecord> m_records;
        std::vector<ScheduledStop> m_scheduledStops;
        int m_step = 0;
        std::uint32_t m_nextId = 1;
    };

    //! @brief 部品の持ち主が居る世界の EffectScene を返す
    //! @param[in] subObject 持ち主の世界を引く部品
    //! @return 持ち主の世界の EffectScene。持ち主か世界が無いか、描画の無い世界なら nullptr
    [[nodiscard]] NS::Gfx::EffectScene* EffectsOf(const NS::Obj::SubObject& subObject) noexcept;
} // namespace GL::Player

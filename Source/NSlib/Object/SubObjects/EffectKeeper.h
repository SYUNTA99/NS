#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/EffectTypes.h"
#include "NSlib/Object/SubObject.h"

#include "NSlib/Core/Types.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Gfx
{
    class EffectScene;
}

namespace NS::Obj
{
    //! @brief 個体が出すと決めたエフェクト 1 つの記録
    //! @details フレームは持ち主の EffectKeeper::Step の数え方で、SceneActor::Update 1 回が 1 フレーム
    struct EffectRecord
    {
        NS::uint32 id = 0;               //!< 記録の番号。1 から振り、同じ EffectKeeper の中で使い回さない
        std::string name;                //!< エフェクトの名前。charge.curl のように組の頭から書く
        int startStep = 0;               //!< 出したフレーム
        std::optional<int> rootStopStep; //!< 親を止めたフレーム。止めていなければ空
        //! 消えたフレーム。残っていれば空。Stop で消したフレームか、Effekseer から消えた更新のフレーム
        std::optional<int> endStep;
        NS::Gfx::EffectHandle handle{}; //!< 再生したエフェクト。描画の無い世界と、絵を読めなかった時は無効
        //! 大きさか量。何を入れるかは出した部品が決める。入れていなければ空
        std::optional<float> amount;
        //! 最後に置いた根の向き。置いていなければ空
        std::optional<NS::Quaternion> rotation;
    };

    //! 個体のエフェクト再生と記録。描画先がなくても記録を残す
    //! 利用側は IUseEffect の関数を使う
    class EffectKeeper : public SubObject
    {
    public:
        //! @param[in] keepEndedSteps 消えた記録を残すフレーム数。過ぎた記録は BeginStep が捨てる
        explicit EffectKeeper(int keepEndedSteps = 600) noexcept : m_keepEndedSteps(keepEndedSteps) {}

        //! @brief フレームを 1 つ進め、前のフレームの更新で Effekseer から消えた記録に消えたフレームを書く
        //! @details SceneActor::Update が頭で 1 回呼ぶ。残すフレーム数を過ぎた記録を捨て、StopAfter の予約を消す
        void BeginStep();

        //! 今のフレームの番号。BeginStep を呼ぶ前は 0
        [[nodiscard]] int Step() const noexcept { return m_step; }

        //! 絵を先に読む。描画の無い世界では何もしない
        void Preload(std::string_view name);

        //! @brief エフェクトを 1 つ出し、今のフレームに出した記録を残す
        //! @details 絵を読めていない名前は再生されず、記録は無効なハンドルで残る
        //! @param[in] name Preload に渡した名前
        //! @return 記録の番号
        [[nodiscard]] NS::uint32 Play(std::string_view name, const NS::Gfx::EffectPlayDesc& desc);

        //! @brief 親を止める。出ていた子は寿命まで残る
        //! @details 無い番号、親を止めた後、消えた後なら何もしない
        void StopRoot(NS::uint32 id) noexcept;

        //! @brief 子も含めて消し、消えたフレームを今のフレームにする
        //! @details 無い番号、消えた後なら何もしない
        void Stop(NS::uint32 id) noexcept;

        //! @brief 今から steps フレーム後の BeginStep で、子も含めて消す
        //! @details steps が 0 以下なら次の BeginStep で消す。その時に消えた後なら何もしない
        void StopAfter(NS::uint32 id, int steps);

        //! @brief 位置・向き・大きさを置き直す。無い番号と再生していない記録では何もしない
        //! @param[in] position ワールド座標の位置
        void SetTransform(NS::uint32 id,
                          const NS::Vector3& position,
                          const NS::Quaternion& rotation,
                          const NS::Vector3& scale) noexcept;

        //! @brief 動的入力を 1 つ書き換える。無い番号と再生していない記録では何もしない
        //! @param[in] index 動的入力の番号。0 から 3
        void SetDynamicInput(NS::uint32 id, int index, float value) noexcept;

        //! 大きさか量を記録に書く。無い番号なら何もしない。2 回書くと後の値が残る
        void SetAmount(NS::uint32 id, float amount) noexcept;
        //! 根を置いた向きを記録に書く。無い番号なら何もしない。描画の無い世界でも書く
        void SetRotation(NS::uint32 id, const NS::Quaternion& rotation) noexcept;

        //! @brief 番号の記録を返す
        //! @return 記録。無い番号か、捨てた後なら nullptr
        [[nodiscard]] const EffectRecord* Find(NS::uint32 id) const noexcept;

        //! 出した順に並んだ記録
        [[nodiscard]] const std::vector<EffectRecord>& Records() const noexcept { return m_records; }

        //! @brief 今のフレームに出した名前を、出した順に out の末尾へ足す
        //! @param[out] out 名前を足す先。中身は消さない
        void AppendStartedNames(std::vector<std::string>& out) const;

        //! @brief 今のフレームに出した大きさか量を、AppendStartedNames と同じ順に out の末尾へ足す
        //! @param[out] out 足す先。入れていない記録は空を足す。中身は消さない
        void AppendStartedAmounts(std::vector<std::optional<float>>& out) const;

        //! @brief 残っている名前を、出した順に out の末尾へ足す
        //! @details 同じ名前が 2 つ残っていれば 2 回足す。描画の無い世界では Stop していない全部を足す
        //! @param[out] out 名前を足す先。中身は消さない
        void AppendLiveNames(std::vector<std::string>& out) const;

        NS_REFLECT_NONE(EffectKeeper, SubObject)

    private:
        struct ScheduledStop
        {
            NS::uint32 id = 0;
            int step = 0;
        };

        //! 持ち主の居るシーンの EffectScene。持ち主が無いか描画の無い世界なら nullptr
        [[nodiscard]] NS::Gfx::EffectScene* Effects() const noexcept;
        [[nodiscard]] EffectRecord* FindMutable(NS::uint32 id) noexcept;

        int m_keepEndedSteps;
        std::vector<EffectRecord> m_records;
        std::vector<ScheduledStop> m_scheduledStops;
        int m_step = 0;
        NS::uint32 m_nextId = 1;
    };
} // namespace NS::Obj

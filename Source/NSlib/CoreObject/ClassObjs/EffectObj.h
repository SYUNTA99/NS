#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Gfx/Render/EffectTypes.h"

#include <cstddef>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class EffectScene;
    }
} // namespace NS

namespace NS
{
    //! 個体が出して、まだ止めていないエフェクト 1 つ
    struct EffectRecord
    {
        uint32 id = 0;                      //!< 再生の番号。1 から振り、同じ部品の中で使い回さない
        const Effect* asset = nullptr;      //!< 出した素材
        Gfx::EffectHandle handle{};         //!< 再生したエフェクト。描画の無い世界では無効
        bool follows = false;               //!< 持ち主の根に付いて動く場合 true
        Vector3 scale{1.0f, 1.0f, 1.0f};    //!< 出した時の大きさ。追従で置き直す時に使う
    };

    //! 個体のエフェクトの再生・停止・置き直し・追従を受け持つ部品
    //! 持ち主を覚えない。再生の置き場 effects と根の姿勢は呼ぶたびに受け、effects の nullptr は描画の無い世界。
    //! 利用側は IGetEffect の関数を使う
    class EffectObj : public Object
    {
    public:
        //! エフェクトを 1 つ出し、止めるまで一覧に残す
        //! 描画の無い世界では再生せず、無効なハンドルで一覧に残す
        //! @param[in] asset 参照する素材
        //! @return 再生の番号。nullptr と空の道筋は 0 で、一覧に残さない。
        //!         描画のある世界では、再生できなかった素材も 0 で一覧に残さない
        [[nodiscard]] uint32 Play(Gfx::EffectScene* effects, const Effect* asset, const Gfx::EffectPlayDesc& desc);

        //! Play と同じに出し、Follow のたびに持ち主の根の位置と向きへ置き直す
        //! @param[in] desc 出すフレームの姿勢。大きさは追従の間も保つ
        [[nodiscard]] uint32 PlayFollowing(Gfx::EffectScene* effects,
                                           const Effect* asset,
                                           const Gfx::EffectPlayDesc& desc);

        //! 親を止めて一覧から外す。出ていた子は寿命まで残る。無い番号なら何もしない
        void StopRoot(Gfx::EffectScene* effects, uint32 id) noexcept;

        //! 子も含めて消して一覧から外す。無い番号なら何もしない
        void Stop(Gfx::EffectScene* effects, uint32 id) noexcept;

        //! 一覧の全部の親を止め、一覧を空にする。持ち主が世界から外れる時に SceneActor が呼ぶ
        void StopRootAll(Gfx::EffectScene* effects) noexcept;

        //! 位置・向き・大きさを置き直す。無い番号と再生していない物では何もしない
        //! @param[in] position ワールド座標の位置
        void SetTransform(Gfx::EffectScene* effects,
                          uint32 id,
                          const Vector3& position,
                          const Quaternion& rotation,
                          const Vector3& scale) noexcept;

        //! 動的入力を 1 つ書き換える。無い番号と再生していない物では何もしない
        //! @param[in] index 動的入力の番号。0 から 3
        void SetDynamicInput(Gfx::EffectScene* effects, uint32 id, int index, float value) noexcept;

        //! Effekseer から消えた再生を一覧から外す。SceneActor::Update が頭で 1 回呼ぶ
        //! 描画の無い世界では、追従しない再生を全部外す (出した歩の終わりまでは一覧に残る)
        void DropEnded(const Gfx::EffectScene* effects) noexcept;

        //! 追従する再生を、持ち主の根の位置と向きへ置き直す。SceneActor::Update が体を動かした後に 1 回呼ぶ
        //! @param[in] position 持ち主の根のワールド座標の位置
        void Follow(Gfx::EffectScene* effects, const Vector3& position, const Quaternion& rotation) noexcept;

        //! 出した順に並んだ、止めていない再生
        [[nodiscard]] const std::vector<EffectRecord>& Records() const noexcept { return m_records; }

        NS_REFLECT_NONE(EffectObj, Object)

    private:
        [[nodiscard]] uint32 Start(Gfx::EffectScene* effects,
                                   const Effect* asset,
                                   const Gfx::EffectPlayDesc& desc,
                                   bool follows);
        //! 番号の再生を一覧から外す
        //! @return 外した再生のハンドル。無い番号なら無効なハンドル
        Gfx::EffectHandle TakeOut(uint32 id) noexcept;
        //! 番号の再生の、一覧の中の位置。無ければ一覧の長さ
        [[nodiscard]] std::size_t IndexOf(uint32 id) const noexcept;

        std::vector<EffectRecord> m_records;
        uint32 m_nextId = 1;
    };
} // namespace NS

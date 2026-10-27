#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/EffectName.h"
#include "NSlib/Gfx/Render/EffectTypes.h"

#include "NSlib/Core/Types.h"
#include <initializer_list>

namespace NS
{
    class EffectKeeper;
    struct EffectRecord;

    namespace Gfx
    {
        class EffectScene;
    }

    /// 個体のエフェクト記録を取得するインターフェース。操作は下の関数で行う
    class IGetEffect
    {
    public:
        /// 出したエフェクトを覚える部品。枠 Effect を作っていない個体では nullptr
        [[nodiscard]] virtual EffectKeeper* GetEffectKeeper() const noexcept = 0;
        /// エフェクトを再生する置き場。シーンに居ない間と描画の無い世界では nullptr
        [[nodiscard]] virtual Gfx::EffectScene* GetEffectScene() const noexcept = 0;

    protected:
        ~IGetEffect() = default;
    };

    /// 出す名前の絵を先に読む。EffectKeeper が無ければ何もしない
    /// 持ち主がシーンに入った後の OnStart で呼ぶ
    void PreloadEffects(const IGetEffect* user, std::initializer_list<EffectName> names);

    /// エフェクトを 1 つ出す
    /// @param[in] name PreloadEffects に渡した名前
    /// @return 記録の番号。EffectKeeper が無い時は 0
    [[nodiscard]] uint32 PlayEffect(const IGetEffect* user,
                                        const EffectName& name,
                                        const Gfx::EffectPlayDesc& desc);

    /// 子も含めて消す。無い番号と EffectKeeper が無い時は何もしない
    void StopEffect(const IGetEffect* user, uint32 id) noexcept;

    /// 親だけを止める。出ていた子は寿命まで残る。無い番号と EffectKeeper が無い時は何もしない
    void StopEffectRoot(const IGetEffect* user, uint32 id) noexcept;

    /// 今から steps フレーム後に、子も含めて消す。EffectKeeper が無ければ何もしない
    /// @param[in] steps 持ち主の SceneActor::Update の回数。0 以下なら次の回で消す
    void StopEffectAfter(const IGetEffect* user, uint32 id, int steps);

    /// 位置・向き・大きさを置き直す。無い番号と EffectKeeper が無い時は何もしない
    /// @param[in] position ワールド座標の位置
    void SetEffectTransform(const IGetEffect* user,
                            uint32 id,
                            const Vector3& position,
                            const Quaternion& rotation,
                            const Vector3& scale) noexcept;

    /// 動的入力を 1 つ書き換える。無い番号と EffectKeeper が無い時は何もしない
    /// @param[in] index 動的入力の番号。0 から 3
    void SetEffectDynamicInput(const IGetEffect* user, uint32 id, int index, float value) noexcept;

    /// 大きさか量を記録に書く。無い番号と EffectKeeper が無い時は何もしない
    void SetEffectAmount(const IGetEffect* user, uint32 id, float amount) noexcept;

    /// 根を置いた向きを記録に書く。無い番号と EffectKeeper が無い時は何もしない
    void SetEffectRotation(const IGetEffect* user, uint32 id, const Quaternion& rotation) noexcept;

    /// 番号の記録を返す
    /// @return 記録。無い番号・捨てた後・EffectKeeper が無い時は nullptr。次の SceneActor::Update をまたいで持たない
    [[nodiscard]] const EffectRecord* FindEffect(const IGetEffect* user, uint32 id) noexcept;

    /// 今のフレームの番号。SceneActor::Update の回数。EffectKeeper が無い時は 0
    [[nodiscard]] int GetEffectStep(const IGetEffect* user) noexcept;
} // namespace NS

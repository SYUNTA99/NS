#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/Gfx/Render/EffectTypes.h"

namespace NS
{
    class EffectObj;

    namespace Gfx
    {
        class EffectScene;
    }

    //! 個体のエフェクトの部品と置き場を取得するインターフェース。操作は下の関数で行う
    class IGetEffect
    {
    public:
        //! エフェクトの再生を受け持つ部品。枠 Effect を作っていない個体では nullptr
        [[nodiscard]] virtual EffectObj* GetEffect() const noexcept = 0;
        //! エフェクトを再生する置き場。シーンに居ない間と描画の無い世界では nullptr
        [[nodiscard]] virtual Gfx::EffectScene* GetEffectScene() const noexcept = 0;

    protected:
        ~IGetEffect() = default;
    };

    //! エフェクトを 1 つ出す
    //! @param[in] asset 再生するエフェクト素材
    //! @param[in] position 出すワールド座標の位置。向きは回さず、大きさは 1
    //! @return 再生の番号。捨ててよい。Effect が無い時と再生できなかった時は 0
    uint32 PlayEffect(const IGetEffect* user, const Effect* asset, const Vector3& position);

    //! エフェクトを持ち主の Transform の位置と向きに 1 つ出し、止めるまで毎フレームそこへ置き直す。大きさは 1
    //! @param[in] asset 再生するエフェクト素材
    //! @return 再生の番号。捨ててよい。Effect が無い時と再生できなかった時は 0
    uint32 PlayFollowingEffect(const IGetEffect* user, const Effect* asset);

    //! 子も含めて消す。無い番号と Effect が無い時は何もしない
    void StopEffect(const IGetEffect* user, uint32 id) noexcept;

    //! 親だけを止める。出ていた子は寿命まで残る。無い番号と Effect が無い時は何もしない
    void StopEffectRoot(const IGetEffect* user, uint32 id) noexcept;

    //! @brief 位置・向き・大きさを置き直す。無い番号と Effect が無い時は何もしない
    //! @param[in] position ワールド座標の位置
    void SetEffectTransform(const IGetEffect* user,
                            uint32 id,
                            const Vector3& position,
                            const Quaternion& rotation,
                            const Vector3& scale) noexcept;

    //! @brief 動的入力を 1 つ書き換える。無い番号と Effect が無い時は何もしない
    //! @param[in] index 動的入力の番号。0 から 3
    void SetEffectDynamicInput(const IGetEffect* user, uint32 id, int index, float value) noexcept;
} // namespace NS

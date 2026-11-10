#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

#include <array>

// 当たりで持ち主の描く形だけを動かす 3 つの見た目 (横揺れ・描く形の倍率・衝撃の震え) と、その計算
// 走る物 (NS::HitReactionRun) が毎フレーム持ち主の Model へ書く。根の位置と当たりは動かさない

namespace GL
{
    namespace Level
    {
        //! 描く形を axis の向きへ左右に揺らす横揺れ
        struct BodyShakeDesc
        {
            NS::Vector3 axis{1.0f, 0.0f, 0.0f}; // 揺らす世界の長さ 1 の向き
            float amplitude = 0.0f;       // 最初の振れ幅 (m)
            int length = 0;               // 揺れのフレーム数
            NS::uint32 seed = 0;          // 振れ幅のばらつきの種
            float firstSign = 1.0f;       // 1 フレーム目の向き。負なら −、ほかは +
            int flipFrames = 1;           // 左右を入れ替えるフレーム数。1 未満は 1
            float amplitudeSpread = 0.0f; // 振れ幅のばらつきの割合。0〜1 に収め、振れ幅に (1 − これ) 〜 1 を掛ける

            //! frame フレーム目の、描く形の世界のずれを返す
            //! @param[in] frame 揺れの何フレーム目か。始まりのフレームが 1
            //! @return ずれ。frame が 1 より前か length 以降で 0 ベクトル
            [[nodiscard]] NS::Vector3 OffsetAt(int frame) const noexcept;
        };

        //! 横揺れの、frame フレーム目の振れ幅の包みを返す
        //! 振れ幅 × (1 − frame ÷ length)。BodyShakeOffset の大きさはこれを超えない
        //! @param[in] frame 揺れの何フレーム目か。始まりのフレームが 1
        //! @param[in] length 揺れのフレーム数。0 以下は揺らさない
        //! @return 包み。frame が 1 より前か length 以降は 0
        [[nodiscard]] float BodyShakeReach(int frame, int length, float amplitude) noexcept;

        //! 横揺れの符号付きのずれを返す。BodyShakeReach の範囲で弱め、同じ seed・frame では同じ値
        //! @param[in] frame 揺れの何フレーム目か。始まりのフレームが 1。1 未満か length 以降、length が 0 以下なら 0
        [[nodiscard]] float BodyShakeOffset(const BodyShakeDesc& desc, int frame) noexcept;

        //! 描く倍率の曲線の再生。曲線の横軸は始めたフレームを 0 にしたフレーム数。点の無い曲線は 1
        struct BodyShapeDesc
        {
            NS::Curve along{};            // alongDirection の向きの倍率
            NS::Curve height{};           // 高さの倍率
            NS::Curve side{};             // alongDirection に直角な水平の向きの倍率
            NS::Vector3 alongDirection{}; // 水平の長さ 1 の向き。0 ベクトルなら水平の倍率は掛けない
            int length = 0;               // 再生するフレーム数

            //! frame フレーム目の世界の軸ごとの倍率を返す
            //! 水平の向きの成分の 2 乗で along と side を x と z に混ぜる。軸に沿う向きでは元の倍率のまま
            [[nodiscard]] NS::Vector3 ScaleAt(int frame) const noexcept;
        };

        //! 衝突点から体の裏へ伝わる頂点の震え
        struct BodyTremorDesc
        {
            NS::Vector3 contactOffset{};  // 衝突点の、根の位置からのずれ (m)。体と一緒に動く
            float amplitudePixels = 0.0f; // 振れ幅。高さ referenceHeight ピクセルの画面の上のピクセル数
            float amplitudeRatio = 0.0f;  // 振れ幅の体の半径に対する割合。正ならピクセルの欄より先に使う
            int reachFrames = 0;          // 衝突点から体の一番遠い所へ届くまでのフレーム数
            int length = 0;               // 震えのフレーム数
            float referenceHeight = 0.0f; // ピクセルを数える画面の高さ。0 以下ならピクセルの振れ幅は 0 m
            float bodyLength = 0.0f;      // 衝突点から裏まで届く長さ (m)。0 以下なら全体が同時に震え始める
        };

        //! 1 フレームの震え。Model の描く値 (Model::SetDrawValues) の 4 つの Vector4 に並べ、Shaders/Game/SurfaceHooks.hlsli が読む
        //! 並びは [contactOffset, amplitude] [right, elapsedFrames] [up, framesPerMeter] [ringFrames, 0, 0, 0]
        struct BodyTremor
        {
            NS::Vector3 contactOffset{}; // 根の位置からの衝突点のずれ (m)
            float amplitude = 0.0f;      // 振れ幅 (m)。0 なら震えない
            NS::Vector3 right{};         // 画面の右の世界の長さ 1 の向き
            float elapsedFrames = 0.0f;  // 震え始めからの経過フレーム数
            NS::Vector3 up{};            // 画面の上の世界の長さ 1 の向き
            float framesPerMeter = 0.0f; // 衝突点から 1 m 離れるごとに遅れて震え始めるフレーム数
            float ringFrames = 0.0f;     // 1 か所が震えるフレーム数。0 以下なら震えない

            //! Model::SetDrawValues へ渡す並びにする
            [[nodiscard]] std::array<NS::Vector4, 4> DrawValues() const noexcept;
            //! DrawValues の並びから読み戻す
            [[nodiscard]] static BodyTremor FromDrawValues(const std::array<NS::Vector4, 4>& values) noexcept;
            //! 全部の欄が有限の場合 true。有限でない震えを描くと形が消える
            [[nodiscard]] bool IsFinite() const noexcept;
        };

        //! 描く震えを作る。正の振れ幅の割合をピクセルの欄より先に使う
        //! @param[in] elapsedFrames 始まりを 0 とする経過フレーム数
        //! @param[in] root 今の根の位置。ピクセルの振れ幅をカメラからの距離で m に直す
        //! @return 震え。経過が 0 より前か length 以降、震える期間が無い時は振れ幅 0
        [[nodiscard]] BodyTremor MakeBodyTremor(const BodyTremorDesc& desc,
                                                int elapsedFrames,
                                                const NS::Vector3& root,
                                                const NS::CameraPose& pose) noexcept;

        //! 横揺れを毎フレーム Model::SetDrawOffset へ書く。始めた後の最初の Advance を 1 フレーム目に数え、
        //! 長さの次のフレームで 0 を書いて終わる
        class BodyShakeRun final : public NS::HitReactionRun
        {
        public:
            explicit BodyShakeRun(const BodyShakeDesc& desc) noexcept : m_desc(desc) {}
            [[nodiscard]] bool Advance(const NS::HitReactionSelf& owner) override;
            void Scale(float scale) noexcept override { m_desc.amplitude *= scale; }
            void Stop(const NS::HitReactionSelf& owner) noexcept override;
            [[nodiscard]] static const void* StaticKind() noexcept;
            [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }

        private:
            BodyShakeDesc m_desc;
            int m_frame = 0; // 書いたフレーム。始まりが 1
        };

        //! 描く倍率の曲線を毎フレーム Model::SetDrawShapeScale へ書く。始めた後の最初の Advance を 0 フレーム目に数え、
        //! 長さのフレームで元の倍率へ戻して終わる。振れ幅を持たないので Scale では弱めない
        class BodyShapeRun final : public NS::HitReactionRun
        {
        public:
            explicit BodyShapeRun(const BodyShapeDesc& desc) noexcept : m_desc(desc) {}
            [[nodiscard]] bool Advance(const NS::HitReactionSelf& owner) override;
            void Scale(float) noexcept override {}
            void Stop(const NS::HitReactionSelf& owner) noexcept override;
            [[nodiscard]] static const void* StaticKind() noexcept;
            [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }

        private:
            BodyShapeDesc m_desc;
            int m_frame = -1; // 書いたフレーム。始まりが 0
        };

        //! 震えを毎フレーム Model::SetDrawValues へ書く。始めた後の最初の Advance を 0 フレーム目に数え、
        //! 振れ幅は持ち主のカメラの視点から根までの距離で決める。長さのフレームで振れ幅 0 を書いて終わる
        class BodyTremorRun final : public NS::HitReactionRun
        {
        public:
            explicit BodyTremorRun(const BodyTremorDesc& desc) noexcept : m_desc(desc) {}
            [[nodiscard]] bool Advance(const NS::HitReactionSelf& owner) override;
            void Scale(float scale) noexcept override;
            void Stop(const NS::HitReactionSelf& owner) noexcept override;
            [[nodiscard]] static const void* StaticKind() noexcept;
            [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }

        private:
            BodyTremorDesc m_desc;
            int m_elapsed = -1; // 書いた経過フレーム。始まりが 0
        };
    } // namespace Level
} // namespace GL

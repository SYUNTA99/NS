#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace DirectX
{
    class SoundEffectInstance;
} // namespace DirectX

namespace NS
{
    namespace Audio
    {
        class XA2Sound;

        //! SoundScene が鳴らしている音 1 つを指す番号。0 は無効
        struct SoundHandle
        {
            uint32 value = 0;

            [[nodiscard]] bool IsValid() const noexcept { return value != 0; }
        };

        //! 位置から鳴らす音を聞く点。前と上は長さ 1 で、互いに直交する
        struct SoundListener
        {
            Vector3 position{0.0f, 0.0f, 0.0f};
            Vector3 forward{0.0f, 0.0f, 1.0f};
            Vector3 up{0.0f, 1.0f, 0.0f};
        };

        //! @brief 鳴っている音の全部を持つ。鳴らすと番号を返し、止める・音量を変える時は番号で指す
        //! @details 無効な XA2Sound (音の装置が無い世界で読んだ音) は、ソースボイスを作らずに一覧に残す。
        //!          装置が無くても、始めた・止めたを Exists で読める
        //! @warning 鳴らした XA2Sound より先に破棄するか、StopAll を呼ぶこと
        class SoundScene : private NonCopyable
        {
        public:
            SoundScene() noexcept;
            ~SoundScene();

            //! @brief 音を鳴らし始める
            //! @param[in] sound 鳴らす音。Stop か鳴り終わりまで生きている事
            //! @param[in] volume 音量。非数か負なら 0 にする
            //! @param[in] loop true なら Stop まで繰り返す。false なら 1 回鳴らし、鳴り終わると Update が一覧から外す
            //! @return 鳴らした音の番号。sound が nullptr なら無効な番号
            [[nodiscard]] SoundHandle Play(const XA2Sound* sound, float volume, bool loop = false);

            //! @brief 世界の位置から音を鳴らし始める。リスナーから見た向きで左右に振り、離れるほど小さくする
            //! @param[in] position 音の出る位置。鳴らした後は動かない
            //! @details sound・volume・loop と戻り値は Play と同じ。左右と大きさは Update が毎フレーム計算し直す
            [[nodiscard]] SoundHandle PlayAt(const XA2Sound* sound,
                                             const Vector3& position,
                                             float volume,
                                             bool loop = false);

            //! @brief 位置から鳴らす音を聞く点を置く。次の Update から効く
            //! @param[in] position 聞く位置。非数か無限大なら前の位置のまま
            //! @param[in] forward 前。長さは問わない
            //! @param[in] up 上。長さは問わず、前と直交していなくてよい
            //! @details 前の長さが 0 の時と、上が前と平行の時は、向きを前の値のままにする
            void SetListener(const Vector3& position, const Vector3& forward, const Vector3& up) noexcept;

            [[nodiscard]] const SoundListener& Listener() const noexcept { return m_listener; }

            //! 位置から鳴らす音が、そのままの大きさで届く距離 (m)。これより離れると距離に反比例して小さくなる。
            //! IsUsableFullVolumeDistance が偽の値なら前の値のまま
            void SetFullVolumeDistance(float meters) noexcept;

            //! 届く距離として使える値か。非数・無限大・0 以下は使えない
            [[nodiscard]] static bool IsUsableFullVolumeDistance(float meters) noexcept;

            [[nodiscard]] float FullVolumeDistance() const noexcept { return m_fullVolumeDistance; }

            //! 止めて一覧から外す。無い番号なら何もしない
            void Stop(SoundHandle handle) noexcept;

            void StopAll() noexcept;

            //! 音量を変える。非数か負なら 0 にする。無い番号なら何もしない
            void SetVolume(SoundHandle handle, float volume);

            //! 一覧にある番号か。鳴り終わった 1 回の音は、次の Update までは true
            [[nodiscard]] bool Exists(SoundHandle handle) const noexcept;

            //! 毎フレーム呼ぶ。鳴り終わった 1 回の音を外し、音の機器を繋ぎ直して止まったループを鳴らし直し、
            //! 位置から鳴らした音の左右と大きさを今のリスナーで計算し直す。
            //! ソースボイスの無い 1 回の音は、鳴り終わったかを問えないのでここで外す
            void Update();

        private:
            struct Voice
            {
                uint32 id = 0;
                std::unique_ptr<DirectX::SoundEffectInstance> instance; // 無効な XA2Sound から鳴らした時は nullptr
                bool loop = false;
                bool positional = false; // PlayAt で鳴らした音なら true
                Vector3 position{0.0f, 0.0f, 0.0f};
            };

            //! Play と PlayAt の中身。position が nullptr なら位置なし
            [[nodiscard]] SoundHandle Start(const XA2Sound* sound, float volume, bool loop, const Vector3* position);

            //! 位置から鳴らした音の左右と大きさを、今のリスナーで計算して渡す。
            //! instance のある、PlayAt で鳴らした音にだけ呼ぶ事
            void Apply3D(const Voice& voice) const;

            //! 番号の音の、一覧の中の位置。無ければ一覧の長さ
            [[nodiscard]] std::size_t IndexOf(SoundHandle handle) const noexcept;

            std::vector<Voice> m_voices;
            uint32 m_nextId = 1;
            SoundListener m_listener;
            float m_fullVolumeDistance = 1.0f;
        };
    } // namespace Audio
} // namespace NS

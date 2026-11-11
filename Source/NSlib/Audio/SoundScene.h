#pragma once

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

            //! 止めて一覧から外す。無い番号なら何もしない
            void Stop(SoundHandle handle) noexcept;

            void StopAll() noexcept;

            //! 音量を変える。非数か負なら 0 にする。無い番号なら何もしない
            void SetVolume(SoundHandle handle, float volume);

            //! 一覧にある番号か。鳴り終わった 1 回の音は、次の Update までは true
            [[nodiscard]] bool Exists(SoundHandle handle) const noexcept;

            //! 毎フレーム呼ぶ。鳴り終わった 1 回の音を外し、音の機器を繋ぎ直して止まったループを鳴らし直す。
            //! ソースボイスの無い 1 回の音は、鳴り終わったかを問えないのでここで外す
            void Update();

        private:
            struct Voice
            {
                uint32 id = 0;
                std::unique_ptr<DirectX::SoundEffectInstance> instance; // 無効な XA2Sound から鳴らした時は nullptr
                bool loop = false;
            };

            //! 番号の音の、一覧の中の位置。無ければ一覧の長さ
            [[nodiscard]] std::size_t IndexOf(SoundHandle handle) const noexcept;

            std::vector<Voice> m_voices;
            uint32 m_nextId = 1;
        };
    } // namespace Audio
} // namespace NS

#pragma once

#include "NSlib/Audio/SoundLoop.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/Object.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace NS
{
    namespace Audio
    {
        class AudioDevice;
    } // namespace Audio

    //! 個体が鳴らして、まだ止めていない音 1 つ
    struct SoundRecord
    {
        uint32 id = 0;                          //!< 音の番号。1 から振り、同じ部品の中で使い回さない
        const Sound* asset = nullptr;       //!< 鳴らした素材
        std::unique_ptr<Audio::SoundLoop> loop; //!< 鳴っているソースボイス。装置の無い世界と音の無い素材では nullptr
        float volume = 1.0f;                    //!< 今の音量
    };

    //! 個体が鳴らし続ける音の再生・停止・音量を受け持つ部品
    //! 持ち主も装置も覚えない。装置は呼ぶたびに受け、nullptr は装置を受け取っていないシーン。
    //! 1 回鳴らして終わる音は覚える物が無いので、ここを通さず PlaySound が直に鳴らす。
    //! 利用側は IGetAudio の関数を使う
    class SoundObj : public Object
    {
    public:
        //! 音をループで鳴らし始め、止めるまで一覧に残す
        //! 装置の無い世界と音の無い素材では、ソースボイスを作らずに一覧に残す
        //! @param[in] sound 鳴らす素材。AssetManager が持つ物で、止めるまで生きている事
        //! @param[in] volume Audio::SoundLoop::SetVolume に渡す音量
        //! @return 音の番号。nullptr と空の道筋は 0 で、一覧に残さない
        [[nodiscard]] uint32 PlayLoop(const Audio::AudioDevice* device, const Sound* sound, float volume);

        //! 止めて一覧から外す。無い番号なら何もしない
        void Stop(uint32 id) noexcept;

        //! 一覧の全部を止め、一覧を空にする。持ち主が世界から外れる時に SceneActor が呼ぶ
        void StopAll() noexcept;

        //! 音量を変える。無い番号なら何もしない
        void SetVolume(uint32 id, float volume) noexcept;

        //! 音の機器を繋ぎ直して止まった音を鳴らし直す。鳴っている音には何もしない。
        //! SceneActor::Update が先頭で 1 回呼ぶ
        void Update();

        //! 鳴らした順に並んだ、止めていない音
        [[nodiscard]] const std::vector<SoundRecord>& Records() const noexcept { return m_records; }

        NS_REFLECT_NONE(SoundObj, Object)

    private:
        //! 番号の音の、一覧の中の位置。無ければ一覧の長さ
        [[nodiscard]] std::size_t IndexOf(uint32 id) const noexcept;

        std::vector<SoundRecord> m_records;
        uint32 m_nextId = 1;
    };
} // namespace NS

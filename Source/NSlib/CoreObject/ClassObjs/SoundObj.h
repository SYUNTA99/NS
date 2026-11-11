#pragma once

#include "NSlib/Audio/SoundScene.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/Sound.h"
#include "NSlib/CoreObject/Object.h"

#include <cstddef>
#include <vector>

namespace NS
{
    //! 個体が鳴らして、まだ終わっていない音 1 つ
    struct SoundRecord
    {
        uint32 id = 0;                //!< 音の番号。1 から振り、同じ部品の中で使い回さない
        const Sound* asset = nullptr; //!< 鳴らした素材
        Audio::SoundHandle handle{};  //!< 鳴らした音。音を出さない世界では無効
        bool loop = false;            //!< 止めるまで繰り返す音なら true
        float volume = 1.0f;          //!< 今の音量
    };

    //! 個体が鳴らす音の再生・停止・音量を受け持つ部品
    //! 持ち主を覚えない。鳴らす先 sounds は呼ぶたびに受け、nullptr は音を出さない世界 (装置を受け取っていないシーン)。
    //! 利用側は IGetAudio の関数を使う
    class SoundObj : public Object
    {
    public:
        //! 音を鳴らし始め、終わるまで一覧に残す
        //! 音を出さない世界では、鳴らさずに一覧に残す
        //! @param[in] sound 鳴らす素材。AssetManager が持つ物で、鳴り終わるまで生きている事
        //! @param[in] volume Audio::SoundScene::Play に渡す音量
        //! @param[in] loop true なら Stop まで繰り返す。false なら 1 回鳴らし、鳴り終わると DropEnded が一覧から外す
        //! @return 音の番号。nullptr と空の道筋は 0 で、一覧に残さない
        [[nodiscard]] uint32 Play(Audio::SoundScene* sounds, const Sound* sound, float volume, bool loop);

        //! 世界の位置から音を鳴らし始める。position は Audio::SoundScene::PlayAt に渡す。ほかは Play と同じ
        [[nodiscard]] uint32 PlayAt(
            Audio::SoundScene* sounds, const Sound* sound, const Vector3& position, float volume, bool loop);

        //! 止めて一覧から外す。無い番号なら何もしない
        void Stop(Audio::SoundScene* sounds, uint32 id) noexcept;

        //! 一覧のループの音を全部止め、一覧を空にする。1 回の音は止めず、最後まで鳴る。
        //! 持ち主が世界から外れる時に SceneActor が呼ぶ
        void StopLoopAll(Audio::SoundScene* sounds) noexcept;

        //! 音量を変える。無い番号なら何もしない
        void SetVolume(Audio::SoundScene* sounds, uint32 id, float volume);

        //! 鳴り終わった 1 回の音を一覧から外す。SceneActor::Update が先頭で 1 回呼ぶ
        //! 音を出さない世界では、1 回の音を全部外す (鳴らした歩の終わりまでは一覧に残る)
        void DropEnded(const Audio::SoundScene* sounds) noexcept;

        //! 鳴らした順に並んだ、終わっていない音
        [[nodiscard]] const std::vector<SoundRecord>& Records() const noexcept { return m_records; }

        NS_REFLECT_NONE(SoundObj, Object)

    private:
        //! Play と PlayAt の中身。position が nullptr なら位置なし
        [[nodiscard]] uint32 Start(
            Audio::SoundScene* sounds, const Sound* sound, const Vector3* position, float volume, bool loop);

        //! 番号の音の、一覧の中の位置。無ければ一覧の長さ
        [[nodiscard]] std::size_t IndexOf(uint32 id) const noexcept;

        std::vector<SoundRecord> m_records;
        uint32 m_nextId = 1;
    };
} // namespace NS

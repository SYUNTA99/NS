#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NS::Editor
{
    //! 両端を含む符号付きフレームの範囲。last が first より小さい時は空
    struct TimelineFrameRange
    {
        int first = 0;
        int last = 0;
    };

    //! 事象に依存しない再生時計。画面と下見が同じ frame を読む
    struct TimelinePlayback
    {
        int frame = 0;
        bool playing = false;
        float speed = 1.0f;
        float framesPerSecond = 60.0f; //!< 速さ 1 の時に 1 秒で進むフレーム数
        float carry = 0.0f;            //!< 1 フレームに届いていない進み

        void Tick(float seconds, TimelineFrameRange range) noexcept;
        void Seek(int requestedFrame, TimelineFrameRange range) noexcept;
        void StepBy(int steps, TimelineFrameRange range) noexcept;
        //! 末尾か範囲の外で止まっている時は、先頭へ戻してから再生する
        void Toggle(TimelineFrameRange range) noexcept;
    };

    //! 帯に表示する 1 行。事象の値と適用処理は用途側が持つ
    struct TimelineTrack
    {
        std::string label;
        int start = 0;
        int length = 1;
        std::vector<int> startedFrames; //!< 実際に始まったフレームの印
    };

    //! 追加の折れ線を帯と同じ目盛りに合わせる寸法
    struct TimelineLayout
    {
        int firstFrame = 0;
        float labelWidth = 0.0f;
        float frameWidth = 0.0f;
        float totalWidth = 0.0f;
        float rowHeight = 0.0f;
    };

    //! @brief 再生・停止・コマ送り・速さの操作を描く
    //! @return 操作が押された場合 true、それ以外の場合は false
    bool DrawTimelinePlayback(TimelinePlayback& playback, TimelineFrameRange range, bool enabled = true) noexcept;
    //! @brief 目盛り・帯・選択・再生位置を描く
    //! @details extraRows は同じ区画へ追加の行を描く
    //! seekRange は目盛りで移動できる範囲で、nullopt なら range
    //! @return 目盛りで位置を動かした場合 true、それ以外の場合は false
    bool DrawTimelineTracks(std::span<const TimelineTrack> tracks,
                            TimelineFrameRange range,
                            TimelinePlayback& playback,
                            std::optional<std::size_t>& selected,
                            bool seekEnabled,
                            float extraHeight = 0.0f,
                            const std::function<void(const TimelineLayout&)>& extraRows = {},
                            std::optional<TimelineFrameRange> seekRange = std::nullopt) noexcept;
} // namespace NS::Editor

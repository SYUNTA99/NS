#pragma once

#include "Game/Level/HitTimeline.h"
#include "NSlib/Windows/FileSystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

//! @brief タイムラインの置き場を試しごとの空のディレクトリへ向け、終わったら元へ戻す
//! @details 前の実行が残したファイルは消してから始める。置き場を戻すと読み直すので、Set した中身も消える
class ScopedHitTimelineDirectory
{
public:
    explicit ScopedHitTimelineDirectory(std::string_view name)
        : m_previous(GL::Level::HitTimelineLibrary::Get().Directory())
    {
        using NS::OS::FileSystem;
        m_directory = FileSystem::Combine(
            FileSystem::Combine(FileSystem::Combine(FileSystem::ContentRoot(), "build"), "TestHitTimelines"), name);
        (void)FileSystem::CreateDirectories(m_directory);
        for (const std::string& path : FileSystem::ListFiles(m_directory, ".json"))
        {
            std::error_code error;
            std::filesystem::remove(std::filesystem::path{NS::OS::StringUtils::WideFromUtf8(path)}, error);
        }
        GL::Level::HitTimelineLibrary::Get().SetDirectory(m_directory);
    }

    ~ScopedHitTimelineDirectory() { GL::Level::HitTimelineLibrary::Get().SetDirectory(m_previous); }

    ScopedHitTimelineDirectory(const ScopedHitTimelineDirectory&) = delete;
    ScopedHitTimelineDirectory& operator=(const ScopedHitTimelineDirectory&) = delete;

    //! name.json へ text をそのまま書く
    void WriteFile(std::string_view name, std::string_view text) const
    {
        const std::string path = NS::OS::FileSystem::Combine(m_directory, std::string{name} + ".json");
        const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
        ASSERT_TRUE(NS::OS::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size())));
    }

    //! 真ん中と外れの両方を timeline にする。段に依らない振る舞いを見る試しが使う
    static void SetBothTiers(const GL::Level::HitTimeline& timeline)
    {
        GL::Level::HitTimelineLibrary::Get().Set("center", timeline);
        GL::Level::HitTimelineLibrary::Get().Set("miss", timeline);
    }

private:
    std::string m_previous;
    std::string m_directory;
};

//! @brief 移す前の返りを、止めの長さ stopSteps の当たりについて写したタイムライン
//! @details 止め [1, N]、潰れは止めの間、明け N + 1 に反動と相手を飛ばすと伸び、伸びは 6 フレームで戻す。
//! 白・揺れ・寄り・振動・エフェクトの事象は持たない (自機と相手の動きだけを見る試しが使う)
inline GL::Level::HitTimeline MakeLegacyHitTimeline(int stopSteps)
{
    using namespace GL::Level;
    const float n = static_cast<float>(stopSteps);
    ShapeEvent shape;
    shape.along.count = 3;
    shape.along.keys[0] = NS::Obj::Curve::Key{0.0f, 0.7f};
    shape.along.keys[1] = NS::Obj::Curve::Key{n - 1.0f, 0.7f};
    shape.along.keys[2] = NS::Obj::Curve::Key{n, 1.0f};
    shape.height.count = 5;
    shape.height.keys[0] = NS::Obj::Curve::Key{0.0f, 1.1f};
    shape.height.keys[1] = NS::Obj::Curve::Key{n - 1.0f, 1.1f};
    shape.height.keys[2] = NS::Obj::Curve::Key{n, 1.2f};
    shape.height.keys[3] = NS::Obj::Curve::Key{n + 3.0f, 0.9f};
    shape.height.keys[4] = NS::Obj::Curve::Key{n + 6.0f, 1.0f};
    HitTimeline timeline;
    timeline.events = {
        {HitStopEvent{}, 1, stopSteps, HitDirection::Any},
        {shape, 1, stopSteps + 6, HitDirection::Any},
        {TargetFreezeEvent{}, 1, stopSteps, HitDirection::Any},
        {ReboundEvent{}, stopSteps + 1, 1, HitDirection::Any},
        {TargetLaunchEvent{}, stopSteps + 1, 1, HitDirection::Any},
    };
    return timeline;
}

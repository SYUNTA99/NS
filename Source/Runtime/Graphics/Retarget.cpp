#include "Runtime/Graphics/Retarget.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"

#include <cctype>
#include <unordered_map>
#include <utility>

namespace NS::Graphics
{
    std::string NormalizeBoneName(std::string_view raw)
    {
        // 最後の : / | より後ろが素の骨名
        const std::size_t separator = raw.find_last_of(":|");
        if (separator != std::string_view::npos)
            raw = raw.substr(separator + 1);

        while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front())) != 0)
            raw.remove_prefix(1);
        while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back())) != 0)
            raw.remove_suffix(1);

        std::string normalized;
        normalized.reserve(raw.size());
        for (const char c : raw)
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return normalized;
    }

    std::vector<AnimationClip> BindClipsByName(const std::vector<AnimationClip>& sourceClips,
                                               const Skeleton& sourceSkeleton,
                                               const Skeleton& targetSkeleton)
    {
        // target の正規化名 → 骨 index
        std::unordered_map<std::string, int> targetIndexByName;
        const std::vector<Bone>& targetBones = targetSkeleton.Bones();
        for (std::size_t i = 0; i < targetBones.size(); ++i)
        {
            const bool inserted =
                targetIndexByName.try_emplace(NormalizeBoneName(targetBones[i].name), static_cast<int>(i)).second;
            if (!inserted)
                NS_LOG_WARN(Graphics, "BindClipsByName: 正規化後の骨名が重複、 先の骨へ張る: {}", targetBones[i].name);
        }

        const std::vector<Bone>& sourceBones = sourceSkeleton.Bones();
        std::vector<AnimationClip> bound;
        bound.reserve(sourceClips.size());
        for (const AnimationClip& clip : sourceClips)
        {
            AnimationClip remappedClip;
            remappedClip.name = clip.name;
            remappedClip.duration = clip.duration;
            for (const BoneTrack& track : clip.tracks)
            {
                if (track.boneIndex < 0 || static_cast<std::size_t>(track.boneIndex) >= sourceBones.size())
                    continue;
                const std::string& sourceName = sourceBones[static_cast<std::size_t>(track.boneIndex)].name;
                const auto found = targetIndexByName.find(NormalizeBoneName(sourceName));
                if (found == targetIndexByName.end())
                    continue;
                BoneTrack remappedTrack = track;
                remappedTrack.boneIndex = found->second;
                remappedClip.tracks.push_back(std::move(remappedTrack));
            }

            // 1 本も結合できないクリップは残しても bind ポーズしか出せず、添字選択をずらすだけなので除外する
            if (remappedClip.tracks.empty())
            {
                NS_LOG_WARN(Graphics, "BindClipsByName: 一致する骨が無いためクリップを除外: {}", clip.name);
                continue;
            }
            bound.push_back(std::move(remappedClip));
        }
        return bound;
    }
} // namespace NS::Graphics

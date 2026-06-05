#include "Framework/Graphics/Retarget.h"

#include "Framework/Graphics/GltfLoader.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace NS::Graphics
{
    std::string NormalizeBoneName(std::string_view raw)
    {
        // 名前空間 prefix を落とす (最後の ':' または '|' より後ろを採る)
        const std::size_t cut = raw.find_last_of(":|");
        std::string_view core = (cut == std::string_view::npos) ? raw : raw.substr(cut + 1);

        // 前後の空白を除去
        const std::size_t begin = core.find_first_not_of(" \t\r\n");
        if (begin == std::string_view::npos)
            return {};
        const std::size_t end = core.find_last_not_of(" \t\r\n");
        core = core.substr(begin, end - begin + 1);

        std::string out(core);
        std::transform(
            out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    std::vector<AnimationClip> BindClipsByName(const Skeleton& source,
                                               const std::vector<AnimationClip>& clips,
                                               const Skeleton& target)
    {
        // 正規化名 → target 骨 index。 重複名は最初を優先 (emplace は既存を上書きしない)
        std::unordered_map<std::string, int> targetByName;
        const std::vector<Bone>& targetBones = target.Bones();
        for (int i = 0; i < static_cast<int>(targetBones.size()); ++i)
        {
            std::string key = NormalizeBoneName(targetBones[i].name);
            if (!key.empty())
                targetByName.emplace(std::move(key), i);
        }

        const std::vector<Bone>& sourceBones = source.Bones();
        std::vector<AnimationClip> out;
        out.reserve(clips.size());
        std::size_t totalMatched = 0;
        for (const AnimationClip& clip : clips)
        {
            AnimationClip bound;
            bound.name = clip.name;
            bound.duration = clip.duration;
            for (const BoneTrack& track : clip.tracks)
            {
                if (track.boneIndex < 0 || track.boneIndex >= static_cast<int>(sourceBones.size()))
                    continue;
                const std::string key = NormalizeBoneName(sourceBones[static_cast<std::size_t>(track.boneIndex)].name);
                if (key.empty())
                    continue;
                const auto it = targetByName.find(key);
                if (it == targetByName.end())
                    continue; // target に同名骨が無いトラックは捨てる (R-8)
                BoneTrack copy = track;
                copy.boneIndex = it->second;
                bound.tracks.push_back(std::move(copy));
                ++totalMatched;
            }
            if (bound.IsValid())
                out.push_back(std::move(bound));
        }

        if (totalMatched == 0)
            NS_LOG_WARN(::NS::Core::LogCat::Graphics, "BindClipsByName: 骨名が 1 つも一致しなかった (リグ不一致?)");
        return out;
    }

    std::vector<AnimationClip> LoadAnimationsForSkeleton(const std::string& path, const Skeleton& target)
    {
        const AnimationSource source = LoadGltfAnimationSource(path);
        if (!source.IsValid())
            return {};
        // 第1段は同名リグの再 index。 異リグの retarget 分岐は後段で足す
        return BindClipsByName(source.skeleton, source.animations, target);
    }
} // namespace NS::Graphics

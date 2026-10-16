#include "NSlib/Graphics/Retarget.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"

#include <cctype>
#include <unordered_map>
#include <utility>

namespace NS
{
    namespace Gfx
    {
        std::string NormalizeBoneName(std::string_view raw)
        {
            // 最後の : / | より後ろが素の骨名
            const std::size_t separator = raw.find_last_of(":|");
            if (separator != std::string_view::npos)
            {
                raw = raw.substr(separator + 1);
            }

            while (!raw.empty() && std::isspace(static_cast<uint8>(raw.front())) != 0)
            {
                raw.remove_prefix(1);
            }
            while (!raw.empty() && std::isspace(static_cast<uint8>(raw.back())) != 0)
            {
                raw.remove_suffix(1);
            }

            std::string normalized;
            normalized.reserve(raw.size());
            for (const char c : raw)
            {
                normalized.push_back(static_cast<char>(std::tolower(static_cast<uint8>(c))));
            }
            return normalized;
        }

        std::vector<int> FindTrackBones(const AnimClip& clip, const Skeleton& skeleton)
        {
            std::unordered_map<std::string, int> indexByName;
            const std::vector<Bone>& bones = skeleton.Bones();
            for (std::size_t i = 0; i < bones.size(); ++i)
            {
                const bool inserted = indexByName.try_emplace(NormalizeBoneName(bones[i].name), static_cast<int>(i)).second;
                if (!inserted)
                {
                    NS_LOG_WARN(Graphics, "FindTrackBones: 正規化後の骨名が重複、 先の骨を使う: {}", bones[i].name);
                }
            }

            std::vector<int> trackBones;
            trackBones.reserve(clip.tracks.size());
            for (const BoneTrack& track : clip.tracks)
            {
                const std::unordered_map<std::string, int>::const_iterator found =
                    indexByName.find(NormalizeBoneName(track.boneName));
                if (found == indexByName.end())
                {
                    trackBones.push_back(-1);
                }
                else
                {
                    trackBones.push_back(found->second);
                }
            }
            return trackBones;
        }
    } // namespace Gfx
} // namespace NS

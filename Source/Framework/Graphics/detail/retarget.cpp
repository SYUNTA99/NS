#include "Framework/Graphics/Retarget.h"

#include "Framework/Graphics/GltfLoader.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace NS::Graphics
{
    namespace
    {
        bool Contains(std::string_view text, std::string_view sub) noexcept
        {
            return text.find(sub) != std::string_view::npos;
        }

        bool HasSuffix(std::string_view text, std::string_view suffix) noexcept
        {
            return text.size() >= suffix.size() &&
                   text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        enum class BoneSide
        {
            None,
            Left,
            Right
        };

        BoneSide DetectSide(std::string_view name) noexcept
        {
            if (Contains(name, "left") || HasSuffix(name, "_l") || HasSuffix(name, ".l"))
                return BoneSide::Left;
            if (Contains(name, "right") || HasSuffix(name, "_r") || HasSuffix(name, ".r"))
                return BoneSide::Right;
            return BoneSide::None;
        }

        // 左右トークンを除いて英数字だけ残した語幹。 末尾の側文字 (例 upperarm_l→upperarml) は
        // 後段が部分一致で見るので残っていても害はない
        std::string CoreToken(std::string_view name)
        {
            std::string work(name);
            for (std::string_view side : {std::string_view("left"), std::string_view("right")})
            {
                std::size_t pos;
                while ((pos = work.find(side)) != std::string::npos)
                    work.erase(pos, side.size());
            }
            std::string core;
            core.reserve(work.size());
            for (char c : work)
                if (std::isalnum(static_cast<unsigned char>(c)))
                    core.push_back(c);
            return core;
        }

        // 正規化骨名の一致率で同一リグかを判定する。 半分以上一致すれば再 index で足りるとみなす
        bool IsSameRig(const Skeleton& source, const Skeleton& target)
        {
            std::unordered_set<std::string> targetNames;
            for (const Bone& bone : target.Bones())
            {
                std::string name = NormalizeBoneName(bone.name);
                if (!name.empty())
                    targetNames.insert(std::move(name));
            }
            if (targetNames.empty())
                return false;

            std::size_t named = 0;
            std::size_t matched = 0;
            for (const Bone& bone : source.Bones())
            {
                const std::string name = NormalizeBoneName(bone.name);
                if (name.empty())
                    continue;
                ++named;
                if (targetNames.count(name) > 0)
                    ++matched;
            }
            return named > 0 && matched * 2 >= named;
        }
    } // namespace

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

    HumanoidBone GuessHumanoidBone(std::string_view normalized) noexcept
    {
        const BoneSide side = DetectSide(normalized);
        const std::string core = CoreToken(normalized);

        auto sided = [side](HumanoidBone left, HumanoidBone right) noexcept {
            if (side == BoneSide::Left)
                return left;
            if (side == BoneSide::Right)
                return right;
            return HumanoidBone::Count; // 側が判らない四肢は対応づけ不可
        };

        // 四肢は語幹に複数トークンが含まれる (forearm⊃arm、 upleg⊃leg) ので限定的な語から先に判定する
        if (Contains(core, "shoulder") || Contains(core, "clavicle"))
            return sided(HumanoidBone::LeftShoulder, HumanoidBone::RightShoulder);
        if (Contains(core, "forearm") || Contains(core, "lowerarm"))
            return sided(HumanoidBone::LeftLowerArm, HumanoidBone::RightLowerArm);
        if (Contains(core, "hand"))
            return sided(HumanoidBone::LeftHand, HumanoidBone::RightHand);
        if (Contains(core, "upperarm") || Contains(core, "arm"))
            return sided(HumanoidBone::LeftUpperArm, HumanoidBone::RightUpperArm);
        if (Contains(core, "upperleg") || Contains(core, "upleg") || Contains(core, "thigh"))
            return sided(HumanoidBone::LeftUpperLeg, HumanoidBone::RightUpperLeg);
        if (Contains(core, "lowerleg") || Contains(core, "calf") || Contains(core, "shin"))
            return sided(HumanoidBone::LeftLowerLeg, HumanoidBone::RightLowerLeg);
        if (Contains(core, "foot") || Contains(core, "ankle"))
            return sided(HumanoidBone::LeftFoot, HumanoidBone::RightFoot);
        if (Contains(core, "leg"))
            return sided(HumanoidBone::LeftLowerLeg, HumanoidBone::RightLowerLeg);

        // 中央ボーン (側なし)。 chest は spine の上位なので spine より先に見る
        if (Contains(core, "chest") || Contains(core, "spine2") || Contains(core, "spine02") ||
            Contains(core, "spine3") || Contains(core, "spine03"))
            return HumanoidBone::Chest;
        if (Contains(core, "spine"))
            return HumanoidBone::Spine;
        if (Contains(core, "neck"))
            return HumanoidBone::Neck;
        if (Contains(core, "head"))
            return HumanoidBone::Head;
        if (Contains(core, "hips") || Contains(core, "pelvis"))
            return HumanoidBone::Hips;

        return HumanoidBone::Count;
    }

    HumanoidMap BuildHumanoidMap(const Skeleton& skeleton)
    {
        HumanoidMap map;
        map.boneIndex.fill(-1);
        const std::vector<Bone>& bones = skeleton.Bones();
        for (int i = 0; i < static_cast<int>(bones.size()); ++i)
        {
            const HumanoidBone hb = GuessHumanoidBone(NormalizeBoneName(bones[static_cast<std::size_t>(i)].name));
            if (hb == HumanoidBone::Count)
                continue;
            int& slot = map.boneIndex[static_cast<std::size_t>(hb)];
            if (slot < 0)
                slot = i; // 配列は topological 順なので先勝ちで親側を採る
        }
        return map;
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

    std::vector<AnimationClip> RetargetClips(const Skeleton& source,
                                             const std::vector<AnimationClip>& clips,
                                             const Skeleton& target,
                                             float resampleFps)
    {
        using NS::Math::Matrix;
        using NS::Math::Quaternion;

        const std::vector<Bone>& srcBones = source.Bones();
        const std::vector<Bone>& dstBones = target.Bones();
        const std::size_t srcCount = srcBones.size();
        const std::size_t dstCount = dstBones.size();
        if (srcCount == 0 || dstCount == 0 || clips.empty())
            return {};

        // target 各骨 → 同じ人型ボーンを担う source 骨 index (無ければ -1)
        const HumanoidMap srcMap = BuildHumanoidMap(source);
        const HumanoidMap dstMap = BuildHumanoidMap(target);
        std::vector<int> targetToSource(dstCount, -1);
        int mappedCount = 0;
        for (std::size_t hb = 0; hb < static_cast<std::size_t>(HumanoidBone::Count); ++hb)
        {
            const int dst = dstMap.boneIndex[hb];
            const int src = srcMap.boneIndex[hb];
            if (dst >= 0 && src >= 0)
            {
                targetToSource[static_cast<std::size_t>(dst)] = src;
                ++mappedCount;
            }
        }
        if (mappedCount == 0)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "RetargetClips: 人型ボーンが 1 つも対応づかなかった (source/target のリグ命名を確認)");
            return {};
        }

        // 共通空間 (root 上位変換を打ち消し) の rest world 回転
        std::vector<BonePose> srcBind(srcCount);
        for (std::size_t i = 0; i < srcCount; ++i)
            srcBind[i] = srcBones[i].bindLocal;
        std::vector<BonePose> dstBind(dstCount);
        for (std::size_t i = 0; i < dstCount; ++i)
            dstBind[i] = dstBones[i].bindLocal;

        std::vector<Matrix> srcRestWorld;
        std::vector<Matrix> dstRestWorld;
        source.ComputeGlobals(srcBind, srcRestWorld, false);
        target.ComputeGlobals(dstBind, dstRestWorld, false);
        // 並進/スケールを落とした純回転行列。 合成は ComputeGlobals と同じ行ベクトル規約 (world = local * parent)
        // に揃え、 直交なので逆行列は転置で取る
        auto rotationOnly = [](const Matrix& m) {
            return Matrix::CreateFromQuaternion(Quaternion::CreateFromRotationMatrix(m));
        };
        std::vector<Matrix> srcRestRot(srcCount);
        for (std::size_t i = 0; i < srcCount; ++i)
            srcRestRot[i] = rotationOnly(srcRestWorld[i]);
        std::vector<Matrix> dstRestRot(dstCount);
        for (std::size_t i = 0; i < dstCount; ++i)
            dstRestRot[i] = rotationOnly(dstRestWorld[i]);

        // Hips は並進もリターゲットする。 体型差を hip 高さ比で吸収し、 他骨は target rest 並進 (骨長) を維持する
        const int dstHips = dstMap.boneIndex[static_cast<std::size_t>(HumanoidBone::Hips)];
        const int srcHips = srcMap.boneIndex[static_cast<std::size_t>(HumanoidBone::Hips)];
        const bool retargetHip = (dstHips >= 0 && srcHips >= 0);
        float hipScale = 1.0f;
        NS::Math::Vector3 srcHipRest{};
        NS::Math::Vector3 dstHipRest{};
        if (retargetHip)
        {
            const float srcHipHeight = srcRestWorld[static_cast<std::size_t>(srcHips)].Translation().y;
            const float dstHipHeight = dstRestWorld[static_cast<std::size_t>(dstHips)].Translation().y;
            if (std::fabs(srcHipHeight) > 1e-4f)
                hipScale = dstHipHeight / srcHipHeight;
            srcHipRest = srcBones[static_cast<std::size_t>(srcHips)].bindLocal.translation;
            dstHipRest = dstBones[static_cast<std::size_t>(dstHips)].bindLocal.translation;
        }

        const float fps = (resampleFps > 0.0f) ? resampleFps : 30.0f;

        std::vector<BonePose> srcPose;
        std::vector<Matrix> srcAnimWorld;
        std::vector<Matrix> srcAnimRot(srcCount);
        std::vector<Matrix> dstGoalRot(dstCount, Matrix::Identity);

        std::vector<AnimationClip> out;
        out.reserve(clips.size());
        for (const AnimationClip& clip : clips)
        {
            if (clip.duration <= 0.0f)
                continue;
            const int frames = std::max(2, static_cast<int>(std::ceil(clip.duration * fps)) + 1);

            AnimationClip rc;
            rc.name = clip.name;
            rc.duration = clip.duration;

            // 対応づいた target 骨だけ rotation track を持たせる (未対応骨は bindLocal 据置)
            std::vector<int> boneToTrack(dstCount, -1);
            for (std::size_t i = 0; i < dstCount; ++i)
            {
                if (targetToSource[i] < 0)
                    continue;
                boneToTrack[i] = static_cast<int>(rc.tracks.size());
                BoneTrack tr;
                tr.boneIndex = static_cast<int>(i);
                tr.rotationInterp = Interpolation::Linear;
                tr.rotationTimes.reserve(static_cast<std::size_t>(frames));
                tr.rotationValues.reserve(static_cast<std::size_t>(frames));
                rc.tracks.push_back(std::move(tr));
            }

            for (int f = 0; f < frames; ++f)
            {
                const float t = clip.duration * static_cast<float>(f) / static_cast<float>(frames - 1);
                SampleClipPose(clip, source, t, srcPose);
                source.ComputeGlobals(srcPose, srcAnimWorld, false);
                for (std::size_t i = 0; i < srcCount; ++i)
                    srcAnimRot[i] = rotationOnly(srcAnimWorld[i]);

                for (std::size_t i = 0; i < dstCount; ++i)
                {
                    const int parent = dstBones[i].parentIndex;
                    const Matrix parentGoal = (parent >= 0 && static_cast<std::size_t>(parent) < i)
                                                  ? dstGoalRot[static_cast<std::size_t>(parent)]
                                                  : Matrix::Identity;
                    const int src = targetToSource[i];
                    Matrix goalWorld;
                    if (src >= 0)
                    {
                        // world 回転デルタ (rest→anim) を target rest に乗せる。 同一リグなら rest
                        // が打ち消し合い直結果へ縮退
                        const Matrix delta = srcRestRot[static_cast<std::size_t>(src)].Transpose() *
                                             srcAnimRot[static_cast<std::size_t>(src)];
                        goalWorld = dstRestRot[i] * delta;
                    }
                    else
                    {
                        goalWorld = Matrix::CreateFromQuaternion(dstBones[i].bindLocal.rotation) * parentGoal;
                    }
                    dstGoalRot[i] = goalWorld;

                    if (boneToTrack[i] >= 0)
                    {
                        const Matrix localRot = goalWorld * parentGoal.Transpose();
                        BoneTrack& tr = rc.tracks[static_cast<std::size_t>(boneToTrack[i])];
                        tr.rotationTimes.push_back(t);
                        tr.rotationValues.push_back(Quaternion::CreateFromRotationMatrix(localRot));
                    }
                }

                if (retargetHip && boneToTrack[static_cast<std::size_t>(dstHips)] >= 0)
                {
                    const NS::Math::Vector3 move = srcPose[static_cast<std::size_t>(srcHips)].translation - srcHipRest;
                    BoneTrack& tr = rc.tracks[static_cast<std::size_t>(boneToTrack[static_cast<std::size_t>(dstHips)])];
                    tr.positionInterp = Interpolation::Linear;
                    tr.positionTimes.push_back(t);
                    tr.positionValues.push_back(dstHipRest + move * hipScale);
                }
            }

            if (rc.IsValid())
                out.push_back(std::move(rc));
        }

        if (out.empty())
            NS_LOG_WARN(::NS::Core::LogCat::Graphics, "RetargetClips: リターゲット結果が空 (尺 0 か対応骨なし)");
        return out;
    }

    std::vector<AnimationClip> LoadAnimationsForSkeleton(const std::string& path, const Skeleton& target)
    {
        const AnimationSource source = LoadGltfAnimationSource(path);
        if (!source.IsValid())
            return {};
        // 同名リグなら再 index で十分、 異リグは人型プロファイルでフルリターゲットする
        if (IsSameRig(source.skeleton, target))
            return BindClipsByName(source.skeleton, source.animations, target);
        return RetargetClips(source.skeleton, source.animations, target);
    }
} // namespace NS::Graphics

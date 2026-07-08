#include "Framework/Graphics/GltfLoader.h"

#include "Framework/Graphics/Animation.h"
#include "Framework/Graphics/detail/gltf_skin_helpers.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#include <algorithm>

#define CGLTF_IMPLEMENTATION
#pragma warning(push, 0)
#include "cgltf.h"
#pragma warning(pop)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NS::Graphics
{
    namespace
    {
        struct CgltfGuard
        {
            cgltf_data* data = nullptr;
            ~CgltfGuard()
            {
                if (data != nullptr)
                    cgltf_free(data);
            }
        };

        const cgltf_accessor* FindAttribute(const cgltf_primitive& prim, cgltf_attribute_type type, cgltf_int setIndex)
        {
            for (cgltf_size i = 0; i < prim.attributes_count; ++i)
            {
                const cgltf_attribute& attribute = prim.attributes[i];
                if (attribute.type == type && attribute.index == setIndex)
                    return attribute.data;
            }
            return nullptr;
        }

        // KHR_draco_mesh_compression が必須拡張に含まれるか。 cgltf_load_buffers は Draco を展開しないため、
        // 圧縮ジオメトリは読むと壊れる
        bool RequiresDraco(const cgltf_data& model)
        {
            for (cgltf_size i = 0; i < model.extensions_required_count; ++i)
            {
                const char* ext = model.extensions_required[i];
                if (ext != nullptr && std::strcmp(ext, "KHR_draco_mesh_compression") == 0)
                    return true;
            }
            return false;
        }

        // world 3x3 の逆転置を余因子で求める。列優先 cgltf: 1 行目は
        // m[0]/m[4]/m[8]。後で正規化するので行列式スケール無視可
        void ComputeNormalMatrix(const float m[16], float out[9])
        {
            const float a = m[0], b = m[4], c = m[8];
            const float d = m[1], e = m[5], f = m[9];
            const float g = m[2], h = m[6], i = m[10];
            out[0] = e * i - f * h;
            out[1] = f * g - d * i;
            out[2] = d * h - e * g;
            out[3] = c * h - b * i;
            out[4] = a * i - c * g;
            out[5] = b * g - a * h;
            out[6] = b * f - c * e;
            out[7] = c * d - a * f;
            out[8] = a * e - b * d;
        }

        // NORMAL 無し primitive 向け: 面法線を area-weighted 積算した smooth normal を RH
        // ローカル空間で計算。縮退頂点は (0,0,1)
        std::vector<std::array<float, 3>> ComputeSmoothNormals(const cgltf_primitive& prim,
                                                               const cgltf_accessor& posAcc,
                                                               cgltf_size vertexCount)
        {
            std::vector<std::array<float, 3>> positions(vertexCount, {0.0f, 0.0f, 0.0f});
            for (cgltf_size i = 0; i < vertexCount; ++i)
                cgltf_accessor_read_float(&posAcc, i, positions[i].data(), 3);

            std::vector<std::array<float, 3>> normals(vertexCount, {0.0f, 0.0f, 0.0f});
            const cgltf_size count = (prim.indices != nullptr) ? prim.indices->count : vertexCount;
            for (cgltf_size t = 0; t + 2 < count; t += 3)
            {
                const cgltf_size i0 = (prim.indices != nullptr) ? cgltf_accessor_read_index(prim.indices, t) : t;
                const cgltf_size i1 =
                    (prim.indices != nullptr) ? cgltf_accessor_read_index(prim.indices, t + 1) : t + 1;
                const cgltf_size i2 =
                    (prim.indices != nullptr) ? cgltf_accessor_read_index(prim.indices, t + 2) : t + 2;
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
                    continue;
                const std::array<float, 3>& a = positions[i0];
                const std::array<float, 3>& b = positions[i1];
                const std::array<float, 3>& c = positions[i2];
                const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const float fx = e1[1] * e2[2] - e1[2] * e2[1];
                const float fy = e1[2] * e2[0] - e1[0] * e2[2];
                const float fz = e1[0] * e2[1] - e1[1] * e2[0];
                normals[i0][0] += fx;
                normals[i0][1] += fy;
                normals[i0][2] += fz;
                normals[i1][0] += fx;
                normals[i1][1] += fy;
                normals[i1][2] += fz;
                normals[i2][0] += fx;
                normals[i2][1] += fy;
                normals[i2][2] += fz;
            }
            for (std::array<float, 3>& nrm : normals)
            {
                const float len2 = nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2];
                if (len2 < 1e-12f)
                    nrm = {0.0f, 0.0f, 1.0f};
            }
            return normals;
        }

        // POSITION 必須、三角形以外は呼出元で弾き済み
        void AppendPrimitive(const cgltf_primitive& prim,
                             const float world[16],
                             const std::string& path,
                             MeshGeometry& geom)
        {
            const cgltf_accessor* posAcc = FindAttribute(prim, cgltf_attribute_type_position, 0);
            if (posAcc == nullptr)
                return;
            const cgltf_accessor* uvAcc = FindAttribute(prim, cgltf_attribute_type_texcoord, 0);
            const cgltf_accessor* normalAcc = FindAttribute(prim, cgltf_attribute_type_normal, 0);

            float nm[9];
            ComputeNormalMatrix(world, nm);

            const std::uint32_t baseVertex = static_cast<std::uint32_t>(geom.vertices.size());
            const cgltf_size vertexCount = posAcc->count;

            std::vector<std::array<float, 3>> computedNormals;
            if (normalAcc == nullptr)
            {
                NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                            "LoadGltfMesh: NORMAL 属性が無いため面法線から smooth normal を生成 (path={})",
                            path);
                computedNormals = ComputeSmoothNormals(prim, *posAcc, vertexCount);
            }

            geom.vertices.reserve(geom.vertices.size() + vertexCount);
            for (cgltf_size i = 0; i < vertexCount; ++i)
            {
                float p[3] = {0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(posAcc, i, p, 3);
                // 右手空間の node world 変換を適用してから、 Z 反転で左手へ
                const float wx = world[0] * p[0] + world[4] * p[1] + world[8] * p[2] + world[12];
                const float wy = world[1] * p[0] + world[5] * p[1] + world[9] * p[2] + world[13];
                const float wz = world[2] * p[0] + world[6] * p[1] + world[10] * p[2] + world[14];

                StaticVertex v{};
                v.position = NS::Math::Vector3{wx, wy, -wz};

                float uv[2] = {0.0f, 0.0f};
                if (uvAcc != nullptr)
                    cgltf_accessor_read_float(uvAcc, i, uv, 2);
                v.uv = NS::Math::Vector2{uv[0], uv[1]};

                float n[3] = {0.0f, 0.0f, 1.0f};
                if (normalAcc != nullptr)
                    cgltf_accessor_read_float(normalAcc, i, n, 3);
                else
                {
                    n[0] = computedNormals[i][0];
                    n[1] = computedNormals[i][1];
                    n[2] = computedNormals[i][2];
                }
                const float nx = nm[0] * n[0] + nm[1] * n[1] + nm[2] * n[2];
                const float ny = nm[3] * n[0] + nm[4] * n[1] + nm[5] * n[2];
                const float nz = nm[6] * n[0] + nm[7] * n[1] + nm[8] * n[2];
                NS::Math::Vector3 normal{nx, ny, -nz};
                normal.Normalize();
                v.normal = normal;

                geom.vertices.push_back(v);
            }

            const std::size_t indexStart = geom.indices.size();
            if (prim.indices != nullptr)
            {
                const cgltf_size indexCount = prim.indices->count;
                geom.indices.reserve(geom.indices.size() + indexCount);
                for (cgltf_size i = 0; i < indexCount; ++i)
                    geom.indices.push_back(baseVertex +
                                           static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
            }
            else
            {
                geom.indices.reserve(geom.indices.size() + vertexCount);
                for (cgltf_size i = 0; i < vertexCount; ++i)
                    geom.indices.push_back(baseVertex + static_cast<std::uint32_t>(i));
            }

            // この primitive 分だけ三角形 winding を右手から左手へ反転
            for (std::size_t t = indexStart; t + 2 < geom.indices.size(); t += 3)
                std::swap(geom.indices[t + 1], geom.indices[t + 2]);
        }

        // 三角形以外 / Draco 圧縮 primitive は skip
        void AppendMesh(const cgltf_mesh& mesh, const float world[16], const std::string& path, MeshGeometry& geom)
        {
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
            {
                const cgltf_primitive& prim = mesh.primitives[p];
                if (prim.type != cgltf_primitive_type_triangles)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "LoadGltfMesh: 三角形以外の primitive を skip (path={}, type={})",
                                 path,
                                 static_cast<int>(prim.type));
                    continue;
                }
                if (prim.has_draco_mesh_compression)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "LoadGltfMesh: Draco 圧縮 primitive は未対応のため skip (path={})",
                                 path);
                    continue;
                }
                AppendPrimitive(prim, world, path, geom);
            }
        }
    } // namespace

    MeshGeometry LoadGltfMesh(const std::string& path)
    {
        MeshGeometry geom;

        // cgltf にはメモリを渡す。cgltf 内部の fopen を使わない
        const std::optional<std::vector<std::byte>> bytes = NS::Core::FileSystem::ReadAllBytes(path);
        if (!bytes)
            return geom; // ReadAllBytes 内で NS_LOG_ERROR 済

        cgltf_options options{};
        CgltfGuard guard;
        cgltf_result result = cgltf_parse(&options, bytes->data(), bytes->size(), &guard.data);
        if (result != cgltf_result_success)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "LoadGltfMesh: glTF parse 失敗 (path={}, code={})",
                         path,
                         static_cast<int>(result));
            return geom;
        }

        result = cgltf_load_buffers(&options, guard.data, path.c_str());
        if (result != cgltf_result_success)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "LoadGltfMesh: buffer 読込失敗 (path={}, code={})",
                         path,
                         static_cast<int>(result));
            return geom;
        }

        const cgltf_data& model = *guard.data;
        if (RequiresDraco(model))
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics, "LoadGltfMesh: KHR_draco_mesh_compression は未対応 (path={})", path);
            return geom;
        }

        bool anyNodeMesh = false;
        for (cgltf_size n = 0; n < model.nodes_count; ++n)
        {
            const cgltf_node& node = model.nodes[n];
            if (node.mesh == nullptr)
                continue;
            anyNodeMesh = true;
            float world[16];
            cgltf_node_transform_world(&node, world);
            AppendMesh(*node.mesh, world, path, geom);
        }

        // ノードに紐付かない mesh だけのファイル は identity 変換でフォールバック
        if (!anyNodeMesh)
        {
            const float identity[16] = {
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
            for (cgltf_size m = 0; m < model.meshes_count; ++m)
                AppendMesh(model.meshes[m], identity, path, geom);
        }

        if (geom.vertices.empty())
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "LoadGltfMesh: 有効な三角形ジオメトリが無い (path={})", path);

        return geom;
    }

    namespace
    {
        // skin->joints の中で node が何番目かを返す。見つからなければ -1 で root 扱い
        int FindJointIndex(const cgltf_skin& skin, const cgltf_node* node)
        {
            if (node == nullptr)
                return -1;
            for (cgltf_size i = 0; i < skin.joints_count; ++i)
                if (skin.joints[i] == node)
                    return static_cast<int>(i);
            return -1;
        }

        // joint node の local TRS を RH→LH 変換して返す。 has_* フラグで cgltf 既定値への依存を避ける
        BonePose ReadJointLocalPose(const cgltf_node& node)
        {
            BonePose pose;
            if (node.has_matrix)
            {
                NS::Math::Vector3 scale;
                NS::Math::Quaternion rotation;
                NS::Math::Vector3 translation;
                NS::Math::Matrix local = detail::ReadColumnMajorMatrix(node.matrix);
                local.Decompose(scale, rotation, translation);
                pose.translation = detail::MirrorZ(translation);
                pose.rotation = detail::MirrorQuaternionZ(rotation);
                pose.scale = scale;
            }
            else
            {
                if (node.has_translation)
                    pose.translation = NS::Math::Vector3{node.translation[0], node.translation[1], node.translation[2]};
                if (node.has_rotation)
                    pose.rotation =
                        NS::Math::Quaternion{node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]};
                if (node.has_scale)
                    pose.scale = NS::Math::Vector3{node.scale[0], node.scale[1], node.scale[2]};
                pose.translation = detail::MirrorZ(pose.translation);
                pose.rotation = detail::MirrorQuaternionZ(pose.rotation);
            }
            return pose;
        }

        // skin から Bone 配列を組む。 ボーン 0 / 上限超過 / inverse bind 欠落・数不一致は false
        bool BuildSkeletonBones(const cgltf_skin& skin, const std::string& path, std::vector<Bone>& outBones)
        {
            if (skin.joints_count == 0)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "LoadGltfSkinnedMesh: skin の joint が 0 (path={})", path);
                return false;
            }
            if (skin.joints_count > kMaxBones)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "LoadGltfSkinnedMesh: ボーン数 {} が上限 {} を超過 (path={})",
                             skin.joints_count,
                             kMaxBones,
                             path);
                return false;
            }
            if (skin.inverse_bind_matrices == nullptr)
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "LoadGltfSkinnedMesh: inverse bind 行列が欠落 (path={})", path);
                return false;
            }
            if (skin.inverse_bind_matrices->count != skin.joints_count)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "LoadGltfSkinnedMesh: inverse bind 数 {} が joint 数 {} と不一致 (path={})",
                             skin.inverse_bind_matrices->count,
                             skin.joints_count,
                             path);
                return false;
            }

            outBones.resize(skin.joints_count);
            for (cgltf_size i = 0; i < skin.joints_count; ++i)
            {
                const cgltf_node* jointNode = skin.joints[i];
                Bone& bone = outBones[i];
                bone.parentIndex = FindJointIndex(skin, jointNode != nullptr ? jointNode->parent : nullptr);
                float ibm[16] = {};
                cgltf_accessor_read_float(skin.inverse_bind_matrices, i, ibm, 16);
                bone.inverseBind = detail::ConjugateZMatrix(detail::ReadColumnMajorMatrix(ibm));
                if (jointNode != nullptr)
                {
                    bone.bindLocal = ReadJointLocalPose(*jointNode);
                    if (jointNode->name != nullptr)
                        bone.name = jointNode->name;
                }
            }
            return true;
        }

        // root joint の親ノード world 変換を LH で返しアーマチュア変換を skinned 出力へ反映。親なしは恒等
        NS::Math::Matrix ComputeSkeletonRootTransform(const cgltf_skin& skin)
        {
            for (cgltf_size i = 0; i < skin.joints_count; ++i)
            {
                const cgltf_node* jointNode = skin.joints[i];
                if (jointNode == nullptr || FindJointIndex(skin, jointNode->parent) != -1)
                    continue;
                const cgltf_node* armature = jointNode->parent;
                if (armature == nullptr)
                    return NS::Math::Matrix::Identity;
                float world[16];
                cgltf_node_transform_world(armature, world);
                return detail::ConjugateZMatrix(detail::ReadColumnMajorMatrix(world));
            }
            return NS::Math::Matrix::Identity;
        }

        // node 変換は焼き込まない。重みのある joint index が範囲外なら false
        bool AppendSkinnedPrimitive(const cgltf_primitive& prim,
                                    cgltf_size jointsCount,
                                    const std::string& path,
                                    std::vector<SkinnedVertex>& vertices,
                                    std::vector<std::uint32_t>& indices)
        {
            const cgltf_accessor* posAcc = FindAttribute(prim, cgltf_attribute_type_position, 0);
            if (posAcc == nullptr)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "LoadGltfSkinnedMesh: POSITION が無い (path={})", path);
                return false;
            }
            const cgltf_accessor* uvAcc = FindAttribute(prim, cgltf_attribute_type_texcoord, 0);
            const cgltf_accessor* normalAcc = FindAttribute(prim, cgltf_attribute_type_normal, 0);
            const cgltf_accessor* jointsAcc = FindAttribute(prim, cgltf_attribute_type_joints, 0);
            const cgltf_accessor* weightsAcc = FindAttribute(prim, cgltf_attribute_type_weights, 0);
            if (jointsAcc == nullptr || weightsAcc == nullptr)
            {
                NS_LOG_ERROR(
                    ::NS::Core::LogCat::Graphics, "LoadGltfSkinnedMesh: JOINTS_0 / WEIGHTS_0 が無い (path={})", path);
                return false;
            }

            const std::uint32_t baseVertex = static_cast<std::uint32_t>(vertices.size());
            const cgltf_size vertexCount = posAcc->count;

            std::vector<std::array<float, 3>> computedNormals;
            if (normalAcc == nullptr)
            {
                NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                            "LoadGltfSkinnedMesh: NORMAL が無いため面法線から smooth normal を生成 (path={})",
                            path);
                computedNormals = ComputeSmoothNormals(prim, *posAcc, vertexCount);
            }

            vertices.reserve(vertices.size() + vertexCount);
            for (cgltf_size i = 0; i < vertexCount; ++i)
            {
                float p[3] = {0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(posAcc, i, p, 3);
                SkinnedVertex v{};
                v.position = detail::MirrorZ(NS::Math::Vector3{p[0], p[1], p[2]});

                float uv[2] = {0.0f, 0.0f};
                if (uvAcc != nullptr)
                    cgltf_accessor_read_float(uvAcc, i, uv, 2);
                v.uv = NS::Math::Vector2{uv[0], uv[1]};

                float n[3] = {0.0f, 0.0f, 1.0f};
                if (normalAcc != nullptr)
                    cgltf_accessor_read_float(normalAcc, i, n, 3);
                else
                {
                    n[0] = computedNormals[i][0];
                    n[1] = computedNormals[i][1];
                    n[2] = computedNormals[i][2];
                }
                NS::Math::Vector3 normal = detail::MirrorZ(NS::Math::Vector3{n[0], n[1], n[2]});
                normal.Normalize();
                v.normal = normal;

                cgltf_uint rawJoints[4] = {0u, 0u, 0u, 0u};
                cgltf_accessor_read_uint(jointsAcc, i, rawJoints, 4);
                float rawWeights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(weightsAcc, i, rawWeights, 4);

                std::array<std::uint32_t, 4> joints{rawJoints[0], rawJoints[1], rawJoints[2], rawJoints[3]};
                std::array<float, 4> weights{rawWeights[0], rawWeights[1], rawWeights[2], rawWeights[3]};
                for (int k = 0; k < 4; ++k)
                {
                    // 重み 0 の枠は index を 0 に倒して GPU の範囲外参照を避ける
                    if (weights[k] == 0.0f)
                    {
                        joints[k] = 0u;
                    }
                    else if (joints[k] >= jointsCount)
                    {
                        NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                     "LoadGltfSkinnedMesh: joint index {} が joint 数 {} の範囲外 (path={})",
                                     joints[k],
                                     jointsCount,
                                     path);
                        return false;
                    }
                }
                detail::NormalizeJointWeights(joints, weights);
                for (int k = 0; k < 4; ++k)
                {
                    v.joints[k] = joints[k];
                    v.weights[k] = weights[k];
                }

                vertices.push_back(v);
            }

            const std::size_t indexStart = indices.size();
            if (prim.indices != nullptr)
            {
                const cgltf_size indexCount = prim.indices->count;
                indices.reserve(indices.size() + indexCount);
                for (cgltf_size i = 0; i < indexCount; ++i)
                    indices.push_back(baseVertex +
                                      static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i)));
            }
            else
            {
                indices.reserve(indices.size() + vertexCount);
                for (cgltf_size i = 0; i < vertexCount; ++i)
                    indices.push_back(baseVertex + static_cast<std::uint32_t>(i));
            }
            for (std::size_t t = indexStart; t + 2 < indices.size(); t += 3)
                std::swap(indices[t + 1], indices[t + 2]);
            return true;
        }

        Interpolation MapInterpolation(cgltf_interpolation_type type) noexcept
        {
            if (type == cgltf_interpolation_type_step)
                return Interpolation::Step;
            return Interpolation::Linear; // linear、 cubic_spline は linear で代替
        }

        // animation channel/sampler を AnimationClip へ変換。resolveBone(node)→bone index は -1
        // で対象外、skinned/source 共用
        template <class ResolveBone>
        void ParseAnimations(const cgltf_data& model,
                             ResolveBone resolveBone,
                             const std::string& path,
                             std::vector<AnimationClip>& outClips)
        {
            for (cgltf_size a = 0; a < model.animations_count; ++a)
            {
                const cgltf_animation& anim = model.animations[a];
                AnimationClip clip;
                clip.name = (anim.name != nullptr) ? anim.name : "";
                float duration = 0.0f;
                std::vector<BoneTrack> tracks;

                auto trackForBone = [&tracks](int boneIndex) -> BoneTrack& {
                    for (BoneTrack& tr : tracks)
                        if (tr.boneIndex == boneIndex)
                            return tr;
                    tracks.push_back(BoneTrack{});
                    tracks.back().boneIndex = boneIndex;
                    return tracks.back();
                };

                for (cgltf_size c = 0; c < anim.channels_count; ++c)
                {
                    const cgltf_animation_channel& channel = anim.channels[c];
                    if (channel.target_node == nullptr || channel.sampler == nullptr)
                        continue;
                    if (channel.target_path == cgltf_animation_path_type_weights)
                        continue; // morph target は非対応
                    const int boneIndex = resolveBone(channel.target_node);
                    if (boneIndex < 0)
                    {
                        NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                                    "glTF animation: target が骨に対応しないため skip (path={})",
                                    path);
                        continue;
                    }

                    const cgltf_animation_sampler& sampler = *channel.sampler;
                    if (sampler.input == nullptr || sampler.output == nullptr || sampler.input->count == 0)
                        continue;
                    const cgltf_size keyCount = sampler.input->count;

                    const bool cubic = (sampler.interpolation == cgltf_interpolation_type_cubic_spline);
                    if (cubic)
                        NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                                    "glTF animation: CUBICSPLINE は未対応のため線形で代替 (path={})",
                                    path);
                    const Interpolation interp = MapInterpolation(sampler.interpolation);
                    const cgltf_size stride = cubic ? 3 : 1;
                    const cgltf_size valueOffset = cubic ? 1 : 0; // cubic は inTangent, value, outTangent の中央

                    std::vector<float> times(keyCount, 0.0f);
                    for (cgltf_size i = 0; i < keyCount; ++i)
                        cgltf_accessor_read_float(sampler.input, i, &times[i], 1);
                    duration = std::max(duration, times.back());

                    BoneTrack& track = trackForBone(boneIndex);
                    if (channel.target_path == cgltf_animation_path_type_translation)
                    {
                        track.positionTimes = times;
                        track.positionInterp = interp;
                        track.positionValues.resize(keyCount);
                        for (cgltf_size i = 0; i < keyCount; ++i)
                        {
                            float v[3] = {0.0f, 0.0f, 0.0f};
                            cgltf_accessor_read_float(sampler.output, i * stride + valueOffset, v, 3);
                            track.positionValues[i] = detail::MirrorZ(NS::Math::Vector3{v[0], v[1], v[2]});
                        }
                    }
                    else if (channel.target_path == cgltf_animation_path_type_rotation)
                    {
                        track.rotationTimes = times;
                        track.rotationInterp = interp;
                        track.rotationValues.resize(keyCount);
                        for (cgltf_size i = 0; i < keyCount; ++i)
                        {
                            float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                            cgltf_accessor_read_float(sampler.output, i * stride + valueOffset, q, 4);
                            track.rotationValues[i] =
                                detail::MirrorQuaternionZ(NS::Math::Quaternion{q[0], q[1], q[2], q[3]});
                        }
                    }
                    else if (channel.target_path == cgltf_animation_path_type_scale)
                    {
                        track.scaleTimes = times;
                        track.scaleInterp = interp;
                        track.scaleValues.resize(keyCount);
                        for (cgltf_size i = 0; i < keyCount; ++i)
                        {
                            float v[3] = {1.0f, 1.0f, 1.0f};
                            cgltf_accessor_read_float(sampler.output, i * stride + valueOffset, v, 3);
                            track.scaleValues[i] = NS::Math::Vector3{v[0], v[1], v[2]};
                        }
                    }
                }

                clip.duration = duration;
                clip.tracks = std::move(tracks);
                if (clip.IsValid())
                    outClips.push_back(std::move(clip));
            }
        }

        // animation 対象 node と祖先から source skeleton を組む。skin 非依存で bones は親先順、outRootXf は LH 上位変換
        bool BuildSourceSkeleton(const cgltf_data& model,
                                 const std::string& path,
                                 std::vector<Bone>& outBones,
                                 std::unordered_map<const cgltf_node*, int>& outNodeToBone,
                                 NS::Math::Matrix& outRootXf)
        {
            std::unordered_set<const cgltf_node*> animated;
            for (cgltf_size a = 0; a < model.animations_count; ++a)
            {
                const cgltf_animation& anim = model.animations[a];
                for (cgltf_size c = 0; c < anim.channels_count; ++c)
                    if (anim.channels[c].target_node != nullptr)
                        animated.insert(anim.channels[c].target_node);
            }
            if (animated.empty())
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "LoadGltfAnimationSource: animation 対象 node が無い (path={})",
                             path);
                return false;
            }

            // global 計算に階層が要るため対象 node とその全祖先を骨格に含める
            std::unordered_set<const cgltf_node*> included;
            for (const cgltf_node* node : animated)
                for (const cgltf_node* p = node; p != nullptr; p = p->parent)
                    included.insert(p);

            outBones.clear();
            outNodeToBone.clear();
            // 親が included でない root から pre-order で並べると親が必ず子より前になる
            std::function<void(const cgltf_node*)> visit = [&](const cgltf_node* node) {
                const int index = static_cast<int>(outBones.size());
                outNodeToBone.emplace(node, index);
                Bone bone;
                const cgltf_node* parent = node->parent;
                bone.parentIndex = (parent != nullptr && included.count(parent) != 0) ? outNodeToBone.at(parent) : -1;
                bone.bindLocal = ReadJointLocalPose(*node);
                if (node->name != nullptr)
                    bone.name = node->name;
                outBones.push_back(std::move(bone)); // inverseBind は恒等のまま、source は skinning しない
                for (cgltf_size i = 0; i < node->children_count; ++i)
                    if (included.count(node->children[i]) != 0)
                        visit(node->children[i]);
            };

            outRootXf = NS::Math::Matrix::Identity;
            bool rootXfSet = false;
            for (cgltf_size n = 0; n < model.nodes_count; ++n)
            {
                const cgltf_node* node = &model.nodes[n];
                if (included.count(node) == 0)
                    continue;
                const bool isRoot = (node->parent == nullptr) || (included.count(node->parent) == 0);
                if (!isRoot)
                    continue;
                // 最初の root の親アーマチュアの world を skeleton 上位変換として採る
                if (!rootXfSet && node->parent != nullptr)
                {
                    float world[16];
                    cgltf_node_transform_world(node->parent, world);
                    outRootXf = detail::ConjugateZMatrix(detail::ReadColumnMajorMatrix(world));
                    rootXfSet = true;
                }
                visit(node);
            }

            if (outBones.empty())
                return false;
            if (outBones.size() > kMaxBones)
            {
                NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                             "LoadGltfAnimationSource: ボーン数 {} が上限 {} を超過 (path={})",
                             outBones.size(),
                             kMaxBones,
                             path);
                return false;
            }
            return true;
        }
    } // namespace

    SkinnedMeshData LoadGltfSkinnedMesh(const std::string& path)
    {
        SkinnedMeshData data;

        const std::optional<std::vector<std::byte>> bytes = NS::Core::FileSystem::ReadAllBytes(path);
        if (!bytes)
            return data;

        cgltf_options options{};
        CgltfGuard guard;
        cgltf_result result = cgltf_parse(&options, bytes->data(), bytes->size(), &guard.data);
        if (result != cgltf_result_success)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "LoadGltfSkinnedMesh: glTF parse 失敗 (path={}, code={})",
                         path,
                         static_cast<int>(result));
            return data;
        }
        result = cgltf_load_buffers(&options, guard.data, path.c_str());
        if (result != cgltf_result_success)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "LoadGltfSkinnedMesh: buffer 読込失敗 (path={}, code={})",
                         path,
                         static_cast<int>(result));
            return data;
        }

        const cgltf_data& model = *guard.data;
        if (RequiresDraco(model))
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "LoadGltfSkinnedMesh: KHR_draco_mesh_compression は未対応 (path={})",
                         path);
            return data;
        }

        // skin を持つ最初の mesh node から skeleton 用の skin を確定する
        const cgltf_node* skinnedNode = nullptr;
        for (cgltf_size n = 0; n < model.nodes_count; ++n)
        {
            const cgltf_node& node = model.nodes[n];
            if (node.mesh != nullptr && node.skin != nullptr)
            {
                skinnedNode = &node;
                break;
            }
        }
        if (skinnedNode == nullptr)
        {
            NS_LOG_ERROR(
                ::NS::Core::LogCat::Graphics, "LoadGltfSkinnedMesh: skin 付き mesh node が無い (path={})", path);
            return data;
        }

        const cgltf_skin& skin = *skinnedNode->skin;

        std::vector<Bone> bones;
        if (!BuildSkeletonBones(skin, path, bones))
            return data;

        // 同一 skin を共有する全 mesh node を連結する。Mixamo は本体と関節マーカーが別 mesh に分かれており、
        // 先頭だけ読むと関節マーカーしか出ない。 joint index 整合のため skin が一致する node のみ対象とする
        std::vector<SkinnedVertex> vertices;
        std::vector<std::uint32_t> indices;
        for (cgltf_size n = 0; n < model.nodes_count; ++n)
        {
            const cgltf_node& node = model.nodes[n];
            if (node.mesh == nullptr || node.skin != &skin)
                continue;
            const cgltf_mesh& mesh = *node.mesh;
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
            {
                const cgltf_primitive& prim = mesh.primitives[p];
                if (prim.type != cgltf_primitive_type_triangles)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "LoadGltfSkinnedMesh: 三角形以外の primitive を skip (path={}, type={})",
                                 path,
                                 static_cast<int>(prim.type));
                    continue;
                }
                if (prim.has_draco_mesh_compression)
                {
                    NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                                 "LoadGltfSkinnedMesh: Draco 圧縮 primitive は未対応のため skip (path={})",
                                 path);
                    continue;
                }
                if (!AppendSkinnedPrimitive(prim, skin.joints_count, path, vertices, indices))
                    return data;
            }
        }

        if (vertices.empty())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "LoadGltfSkinnedMesh: 有効な skinned 三角形ジオメトリが無い (path={})",
                         path);
            return data;
        }

        const std::vector<std::uint32_t> remap = detail::TopologicalSortBones(bones);
        for (SkinnedVertex& v : vertices)
            for (int k = 0; k < 4; ++k)
                v.joints[k] = remap[v.joints[k]];

        data.vertices = std::move(vertices);
        data.indices = std::move(indices);
        data.skeleton = Skeleton(std::move(bones));
        data.skeleton.SetRootTransform(ComputeSkeletonRootTransform(skin));
        ParseAnimations(
            model,
            [&](const cgltf_node* node) -> int {
                const int joint = FindJointIndex(skin, node);
                return (joint < 0) ? -1 : static_cast<int>(remap[static_cast<std::size_t>(joint)]);
            },
            path,
            data.animations);
        return data;
    }

    AnimationSource LoadGltfAnimationSource(const std::string& path)
    {
        AnimationSource source;

        const std::optional<std::vector<std::byte>> bytes = NS::Core::FileSystem::ReadAllBytes(path);
        if (!bytes)
            return source;

        cgltf_options options{};
        CgltfGuard guard;
        cgltf_result result = cgltf_parse(&options, bytes->data(), bytes->size(), &guard.data);
        if (result != cgltf_result_success)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "LoadGltfAnimationSource: glTF parse 失敗 (path={}, code={})",
                         path,
                         static_cast<int>(result));
            return source;
        }
        result = cgltf_load_buffers(&options, guard.data, path.c_str());
        if (result != cgltf_result_success)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics,
                         "LoadGltfAnimationSource: buffer 読込失敗 (path={}, code={})",
                         path,
                         static_cast<int>(result));
            return source;
        }

        const cgltf_data& model = *guard.data;
        if (model.animations_count == 0)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "LoadGltfAnimationSource: animation が無い (path={})", path);
            return source;
        }

        std::vector<Bone> bones;
        std::unordered_map<const cgltf_node*, int> nodeToBone;
        NS::Math::Matrix rootXf = NS::Math::Matrix::Identity;
        if (!BuildSourceSkeleton(model, path, bones, nodeToBone, rootXf))
            return source;

        source.skeleton = Skeleton(std::move(bones));
        source.skeleton.SetRootTransform(rootXf);
        ParseAnimations(
            model,
            [&](const cgltf_node* node) -> int {
                const auto it = nodeToBone.find(node);
                return (it == nodeToBone.end()) ? -1 : it->second;
            },
            path,
            source.animations);
        return source;
    }
} // namespace NS::Graphics

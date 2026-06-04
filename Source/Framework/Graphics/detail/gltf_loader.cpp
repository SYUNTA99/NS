#include "Framework/Graphics/GltfLoader.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

#define CGLTF_IMPLEMENTATION
#pragma warning(push, 0)
#include "cgltf.h"
#pragma warning(pop)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

namespace NS::Graphics
{
    namespace
    {
        // cgltf_data の解放を保証する
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
                const cgltf_attribute& attr = prim.attributes[i];
                if (attr.type == type && attr.index == setIndex)
                    return attr.data;
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

        // world の上 3x3 から法線用の逆転置行列を余因子で求める
        // cgltf の world は列優先格納なので 1 行目は m[0] / m[4] / m[8]、 戻り値 out は 1 行ずつ並べる
        // normal は後で正規化するので行列式スケールは無視でき、 非一様 scale でも歪まない
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

        // NORMAL 属性が無い primitive 向けに、 面法線を頂点へ area-weighted で積算した smooth normal を
        // RH ローカル空間で計算する (winding は glTF CCW のまま)。 戻り値は未正規化で、 呼出側が
        // normal matrix 変換後に正規化する。 縮退三角形しか触れない頂点は (0,0,1) を既定にする
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

        // 1 primitive ぶんの頂点 / index を world 変換しつつ geom に連結する (POSITION 必須、 三角形前提)
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

            // NORMAL 属性が無ければ面法線から smooth normal を自前計算する (RH ローカル空間)
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
                // node の world 変換 (右手空間) を適用してから、 Z 反転で 左手へ
                const float wx = world[0] * p[0] + world[4] * p[1] + world[8] * p[2] + world[12];
                const float wy = world[1] * p[0] + world[5] * p[1] + world[9] * p[2] + world[13];
                const float wz = world[2] * p[0] + world[6] * p[1] + world[10] * p[2] + world[14];

                MeshVertex v{};
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

            // この primitive 分だけ三角形 winding 反転 (右手 -> 左手 )
            for (std::size_t t = indexStart; t + 2 < geom.indices.size(); t += 3)
                std::swap(geom.indices[t + 1], geom.indices[t + 2]);
        }

        // 1 mesh の全 primitive を処理する。 三角形以外 / Draco 圧縮 primitive は skip
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

        // cgltf にはメモリを渡す (cgltf 内部 fopen を使わない)
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

        // シーングラフのノードを辿り、 mesh を持つノードの world 変換でジオメトリを連結する
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
} // namespace NS::Graphics

#pragma once

/// @file InstanceBatcher.h
/// @brief NS::Graphics::InstanceBatcher — block を mesh と material をキーにした bucket へ集約し
/// `DrawIndexedInstanced` で 1 bucket = 1 draw call にまとめる per-frame バッチャ
///
/// @details per-instance VB は slot 1 + `D3D11_INPUT_PER_INSTANCE_DATA`、 stride は
/// `sizeof(BlockInstance) == 80` 固定。 `BlockInstance` は `alignas(16)` で 16 byte 境界に
/// 揃え、 `static_assert` で C++ 側 stride と HLSL InputLayout の AlignedByteOffset が
/// ずれない事をコンパイル時に保証する。 テスト用ヘッドレスなど device 未提供環境でも
/// `BucketCount()` / `LastFrameDrawCallCount()` が観測できる薄い実装に保つ
/// GPU バインドは FlushAll(Renderer&) に渡す Renderer 経由で行い、 DeviceContext は保持しない

#include "Framework/Core/NonCopyable.h"
#include "Framework/Graphics/D3dCommon.h"
#include "Framework/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace NS::Graphics
{

    class StaticMesh;
    class Material;
    class Renderer;
    class Buffer;

    /// slot 1 の per-instance データ。80 byte / 16 byte align で HLSL InputLayout の AlignedByteOffset と一致
    struct alignas(16) BlockInstance
    {
        NS::Math::Matrix worldMatrix{};                // 64 byte: row_major world 行列
        NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f}; // 12 byte: 個体色
        float textureSlice = 0.0f;                     // 4 byte: Texture2DArray slice index
    };
    static_assert(sizeof(BlockInstance) == 80, "BlockInstance stride は 80 byte 固定 (HLSL slot1 layout 整合)");
    static_assert(alignof(BlockInstance) == 16, "BlockInstance は 16 byte align 必須");

    /// mesh と material をキーにした bucket 集約バッチャ。1 bucket = 1 DrawIndexedInstanced、Renderer
    /// より先に破棄すること
    class InstanceBatcher : public NS::Core::NonCopyable
    {
    public:
        /// InstanceBatcher を生成する。 device 未提供環境でも非 null で集約のみ動作する
        [[nodiscard]] static std::unique_ptr<InstanceBatcher> Create();

        ~InstanceBatcher();

        /// 新フレーム開始: 全 bucket の instance 配列を空にする。 Renderer::BeginFrame 直後に呼ぶ
        void BeginFrame() noexcept;

        /// 1 block instance を mesh と material のキーの bucket に追加。mesh / material は非 null 必須
        void Submit(StaticMesh* mesh, Material* material, const BlockInstance& instance);

        /// 全 bucket を DrawIndexedInstanced で発行する。発行後に LastFrameDrawCallCount() が更新される
        void FlushAll(Renderer& renderer) noexcept;

        /// dev の F5 hot reload 用に instanced VS/PS と InputLayout を HLSL から再コンパイルして差し替える
        /// InstanceBatcher の shader は AssetManager 管理外のため個別に必要。 失敗時は旧
        /// shader 維持
        void ReloadShaders() noexcept;

        /// テスト観測用の現フレーム bucket 数
        [[nodiscard]] std::size_t BucketCount() const noexcept;

        /// 直近 FlushAll で発行された DrawIndexedInstanced 回数
        [[nodiscard]] std::size_t LastFrameDrawCallCount() const noexcept;

        /// 内部リソース構築済なら true。 device 無しでもカウント系は機能する
        [[nodiscard]] bool IsValid() const noexcept;

        /// D3D11 draw を抑止しカウントのみ更新するテスト専用モード。製品コードから呼ぶと描画が無音で消える
        void SetCountOnlyMode(bool countOnly) noexcept;

    private:
        InstanceBatcher();

        /// instanced VS/PS をコンパイルし InputLayout を作って member へ commit する。 全段成功時のみ差し替える
        [[nodiscard]] bool BuildShaders() noexcept;

        // アドレス一致で同一 bucket とみなす
        struct BucketKey
        {
            StaticMesh* mesh = nullptr;
            Material* material = nullptr;

            [[nodiscard]] bool operator==(const BucketKey& rhs) const noexcept
            {
                return mesh == rhs.mesh && material == rhs.material;
            }
        };
        struct BucketKeyHash
        {
            [[nodiscard]] std::size_t operator()(const BucketKey& k) const noexcept
            {
                const auto a = reinterpret_cast<std::uintptr_t>(k.mesh);
                const auto b = reinterpret_cast<std::uintptr_t>(k.material);
                // mesh / material の 2 アドレスを xor で混ぜる素朴な hash、 衝突は事実上ゼロ
                return static_cast<std::size_t>(a ^ ((b << 32) | (b >> 32)));
            }
        };
        struct Bucket
        {
            std::vector<BlockInstance> instances{};
        };

        std::unordered_map<BucketKey, Bucket, BucketKeyHash> m_buckets;
        ComPtr<ID3D11Device> m_device;
        std::unique_ptr<Buffer> m_instanceVB;
        ComPtr<ID3D11VertexShader> m_vs;
        ComPtr<ID3D11PixelShader> m_ps;
        ComPtr<ID3D11InputLayout> m_inputLayout;
        std::size_t m_instanceVbCapacity = 0;
        std::size_t m_lastDrawCallCount = 0;
        bool m_valid = false;
        bool m_countOnlyMode = false;
    };

} // namespace NS::Graphics

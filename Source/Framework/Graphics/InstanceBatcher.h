#pragma once

/// @file InstanceBatcher.h
/// @brief NS::Graphics::InstanceBatcher — block を (mesh, material) bucket に集約し
/// `DrawIndexedInstanced` で 1 bucket = 1 draw call にまとめる per-frame バッチャ
///
/// @details per-instance VB は slot 1 + `D3D11_INPUT_PER_INSTANCE_DATA`、 stride は
/// `sizeof(BlockInstance) == 80` 固定。 `BlockInstance` は `alignas(16)` で 16 byte 境界に
/// 揃え、 `static_assert` で C++ 側 stride と HLSL InputLayout の AlignedByteOffset が
/// ずれない事をコンパイル時に保証する。 device 未提供 (テスト用ヘッドレス) 環境でも
/// `BucketCount()` / `LastFrameDrawCallCount()` が観測できる薄い実装に保つ
/// 依存: Renderer の Device / DeviceContext を内部で保持するため Renderer より先に破棄すること

#include "Framework/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace NS::Graphics
{

    class Mesh;
    class Material;
    class Renderer;
    class InstanceBatcher;

    namespace detail
    {
        /// テスト用フック: D3D11 draw 呼出を抑止し、 bucket カウントと draw call カウント
        /// だけを更新する count-only モードを切替える。 production code からは呼ばない
        /// mock pointer を bucket key に渡すユニットテスト (`instance_batcher_test`) 専用
        void SetCountOnlyMode(InstanceBatcher& batcher, bool countOnly) noexcept;
    } // namespace detail

    /// 1 block 1 instance ぶんの per-instance データ (slot 1 入力)
    /// `worldMatrix` 64 byte + `baseColor` 12 byte + `textureSlice` 4 byte = 80 byte 固定
    /// `alignas(16)` で 16 byte 境界に揃え、 HLSL `INSTANCE_WORLD` / `INSTANCE_COLOR` の
    /// AlignedByteOffset (0 / 16 / 32 / 48 / 64) と完全一致させる
    /// `textureSlice` は INSTANCE_COLOR.w に乗せ、 VS 経由で PS の Texture2DArray sample index になる
    struct alignas(16) BlockInstance
    {
        NS::Math::Matrix worldMatrix{};                ///< 64 byte: row_major world 行列
        NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f}; ///< 12 byte: 個体色 (theme tint multiplier)
        float textureSlice = 0.0f;                     ///< 4 byte: Texture2DArray slice index (float で VS->PS 補間)
    };
    static_assert(sizeof(BlockInstance) == 80, "BlockInstance stride は 80 byte 固定 (HLSL slot1 layout 整合)");
    static_assert(alignof(BlockInstance) == 16, "BlockInstance は 16 byte align 必須");

    /// (mesh, material) bucket 集約バッチャ。 1 bucket = 1 `DrawIndexedInstanced`
    /// 同一 Renderer 寿命中だけ有効、 Renderer より先に破棄すること
    /// device 未提供環境では bucket カウントと draw call カウントのみが更新される
    class InstanceBatcher
    {
    public:
        struct Impl;

        explicit InstanceBatcher(Renderer& renderer);
        ~InstanceBatcher();

        InstanceBatcher(const InstanceBatcher&) = delete;
        InstanceBatcher& operator=(const InstanceBatcher&) = delete;
        InstanceBatcher(InstanceBatcher&&) = delete;
        InstanceBatcher& operator=(InstanceBatcher&&) = delete;

        /// 新フレーム開始: 全 bucket の instance 配列を空にする
        /// `Renderer::BeginFrame` の直後に呼ぶ想定
        void BeginFrame() noexcept;

        /// 1 block instance を (mesh, material) bucket に追加
        /// mesh / material は非 null 必須 (null 渡し時は no-op + `NS_LOG_ERROR`)
        /// ownership は呼出側、 batcher は raw ポインタを bucket key として保持するのみ
        void Submit(Mesh* mesh, Material* material, const BlockInstance& instance);

        /// 全 bucket を順次 `DrawIndexedInstanced` で発行する
        /// 発行後に `LastFrameDrawCallCount()` が更新される
        void FlushAll() noexcept;

        /// 現フレームの bucket 数 (テスト観測用)
        [[nodiscard]] std::size_t BucketCount() const noexcept;

        /// 直近 `FlushAll` で発行された `DrawIndexedInstanced` 回数
        /// 1000 block / ≤200 draw call の監視に使う
        [[nodiscard]] std::size_t LastFrameDrawCallCount() const noexcept;

        /// 内部 VB / InputLayout / ShaderProgram が構築済なら true
        /// device 未提供時 (テスト) は false でも `BucketCount` / `LastFrameDrawCallCount`
        /// は機能する (集約ロジックのみ動かしたい単体テストのため)
        [[nodiscard]] bool IsValid() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend void detail::SetCountOnlyMode(InstanceBatcher& batcher, bool countOnly) noexcept;
    };

} // namespace NS::Graphics

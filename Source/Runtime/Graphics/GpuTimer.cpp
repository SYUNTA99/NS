#include "Runtime/Graphics/GpuTimer.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Platform/Clock.h"

#include <chrono>

namespace NS::Gfx
{
    namespace
    {
        // 描画装置が End まで進むのを待つ上限。1 フレームの描画はこれより十分に短い
        constexpr std::chrono::milliseconds k_ReadTimeout{1000};

        // 問い合わせの結果が出るまで待つ。flags 0 で積んだ命令を描画装置へ送り出す
        template <typename T>
        [[nodiscard]] bool WaitForData(ID3D11DeviceContext* context,
                                       ID3D11Query* query,
                                       T& outData,
                                       std::chrono::steady_clock::time_point deadline) noexcept
        {
            while (true)
            {
                const HRESULT hr = context->GetData(query, &outData, sizeof(T), 0);
                if (hr == S_OK)
                {
                    return true;
                }
                if (FAILED(hr) || NS::Platform::Clock::Now() >= deadline)
                {
                    return false;
                }
            }
        }
    } // namespace

    GpuTimer::GpuTimer() noexcept
    {
        ID3D11Device* device = Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_WARN(Graphics, "GpuTimer: Renderer が未構築のため無効のまま作る");
            return;
        }

        D3D11_QUERY_DESC disjointDesc{};
        disjointDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        D3D11_QUERY_DESC timestampDesc{};
        timestampDesc.Query = D3D11_QUERY_TIMESTAMP;
        if (FAILED(device->CreateQuery(&disjointDesc, &m_disjoint)) ||
            FAILED(device->CreateQuery(&timestampDesc, &m_start)) ||
            FAILED(device->CreateQuery(&timestampDesc, &m_end)))
        {
            NS_LOG_ERROR(Graphics, "GpuTimer: 時刻の問い合わせを作れなかった");
            m_disjoint.Reset();
            m_start.Reset();
            m_end.Reset();
        }
    }

    GpuTimer::~GpuTimer() = default;

    bool GpuTimer::IsValid() const noexcept
    {
        return m_disjoint != nullptr && m_start != nullptr && m_end != nullptr;
    }

    void GpuTimer::Begin() noexcept
    {
        ID3D11DeviceContext* context = Gpu().context;
        if (!IsValid() || context == nullptr)
        {
            return;
        }
        // 前の測りを読まずに始め直すと、刻みの問い合わせが Begin のまま 2 回目の Begin を受けて壊れる
        if (m_begun)
        {
            context->End(m_disjoint.Get());
        }
        context->Begin(m_disjoint.Get());
        context->End(m_start.Get());
        m_begun = true;
        m_ended = false;
    }

    void GpuTimer::End() noexcept
    {
        ID3D11DeviceContext* context = Gpu().context;
        if (!IsValid() || context == nullptr || !m_begun)
        {
            return;
        }
        context->End(m_end.Get());
        context->End(m_disjoint.Get());
        m_begun = false;
        m_ended = true;
    }

    std::optional<float> GpuTimer::ReadMilliseconds() noexcept
    {
        ID3D11DeviceContext* context = Gpu().context;
        if (!IsValid() || context == nullptr || !m_ended)
        {
            return std::nullopt;
        }
        m_ended = false;

        const std::chrono::steady_clock::time_point deadline = NS::Platform::Clock::Now() + k_ReadTimeout;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        UINT64 start = 0;
        UINT64 end = 0;
        if (!WaitForData(context, m_disjoint.Get(), disjoint, deadline) ||
            !WaitForData(context, m_start.Get(), start, deadline) || !WaitForData(context, m_end.Get(), end, deadline))
        {
            NS_LOG_WARN(Graphics, "GpuTimer: 描画装置の時刻を読めなかった");
            return std::nullopt;
        }
        // 刻みが途中で変わった区間の差は時間にならない (電源の切り替えなど)
        if (disjoint.Disjoint || disjoint.Frequency == 0 || end < start)
        {
            return std::nullopt;
        }
        const double seconds = static_cast<double>(end - start) / static_cast<double>(disjoint.Frequency);
        return static_cast<float>(seconds * 1000.0);
    }
} // namespace NS::Gfx

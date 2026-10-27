#pragma once

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

//! 作ってから壊すまでに出た ERROR と WARN の文を控える。出力先へ出すのは止めない
//! 既定のロガーを、今の出力先に控えを足したロガーへ差し替え、壊す時に戻す
//! 出力先の一覧を直に書き換えると、spdlog の定期の書き出しのスレッドと競るため
class TestLogCapture
{
public:
    TestLogCapture() : m_previous(spdlog::default_logger())
    {
        std::vector<spdlog::sink_ptr> sinks = m_previous->sinks();
        sinks.push_back(std::make_shared<spdlog::sinks::callback_sink_mt>([this](const spdlog::details::log_msg& msg) {
            if (msg.level == spdlog::level::err)
            {
                const std::lock_guard<std::mutex> lock{m_mutex};
                m_errors.emplace_back(msg.payload.data(), msg.payload.size());
            }
            if (msg.level == spdlog::level::warn)
            {
                const std::lock_guard<std::mutex> lock{m_mutex};
                m_warnings.emplace_back(msg.payload.data(), msg.payload.size());
            }
        }));
        std::shared_ptr<spdlog::logger> logger =
            std::make_shared<spdlog::logger>(m_previous->name(), sinks.begin(), sinks.end());
        logger->set_level(m_previous->level());
        logger->flush_on(m_previous->flush_level());
        spdlog::set_default_logger(logger);
    }

    ~TestLogCapture() { spdlog::set_default_logger(m_previous); }

    TestLogCapture(const TestLogCapture&) = delete;
    TestLogCapture& operator=(const TestLogCapture&) = delete;

    [[nodiscard]] int ErrorCount() const
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return static_cast<int>(m_errors.size());
    }

    [[nodiscard]] int WarningCount() const
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return static_cast<int>(m_warnings.size());
    }

    //! 控えた WARN の文の写し。出た順
    [[nodiscard]] std::vector<std::string> Warnings() const
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        return m_warnings;
    }

private:
    std::shared_ptr<spdlog::logger> m_previous;
    mutable std::mutex m_mutex;
    std::vector<std::string> m_errors;
    std::vector<std::string> m_warnings;
};

#pragma once

#include <Runtime/Core/FileSystem.h>
#include <Runtime/Core/Math.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace NS::Tests
{
    //! @brief 1 固定ステップぶんの記録
    //! @details コヨーテ時間・先行入力の内部の秒は、ジャンプの成否として位置・速度・接地に出る
    //! 基準比較はこの並びを 1 フレームずつ突き合わせる
    struct StepRecord
    {
        NS::Core::Vector3 position{};
        NS::Core::Vector3 velocity{};
        bool grounded = false;
    };

    namespace detail
    {
        //! FNV-1a 64 ビットへ 4 バイトを畳み込む
        [[nodiscard]] inline std::uint64_t FoldFnv1a(std::uint64_t hash, std::uint32_t value) noexcept
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                hash ^= (value >> shift) & 0xFFu;
                hash *= 0x100000001B3ULL;
            }
            return hash;
        }
    } // namespace detail

    //! @brief 記録の並びを 1 つのハッシュへ畳む
    //! @details float はビット表現のまま投入するので、1 ビットでも挙動が変われば値が変わる
    //! @param[in] trace 毎固定ステップの記録
    //! @return 並び全体のハッシュ
    [[nodiscard]] inline std::uint64_t FoldTrace(const std::vector<StepRecord>& trace) noexcept
    {
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        for (const StepRecord& s : trace)
        {
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.position.x));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.position.y));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.position.z));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.velocity.x));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.velocity.y));
            hash = detail::FoldFnv1a(hash, std::bit_cast<std::uint32_t>(s.velocity.z));
            std::uint32_t groundedBit = 0u;
            if (s.grounded)
                groundedBit = 1u;
            hash = detail::FoldFnv1a(hash, groundedBit);
        }
        return hash;
    }

    //! @brief 基準比較で許す差
    //! @details 既定は全て 0 で、完全一致だけを通す
    struct TraceTolerance
    {
        float position = 0.0f;              //!< 1 フレームの位置差の上限 (m)
        float velocity = 0.0f;              //!< 1 フレームの速度差の上限
        std::size_t groundedMismatches = 0; //!< 接地の食い違いを許すフレーム数
    };

    //! @brief 基準と実測の食い違い
    struct TraceDiff
    {
        std::size_t baselineSteps = 0;      //!< 基準のフレーム数
        std::size_t actualSteps = 0;        //!< 実測のフレーム数
        float maxPositionDelta = 0.0f;      //!< 位置差の最大 (m)
        float maxVelocityDelta = 0.0f;      //!< 速度差の最大
        std::size_t groundedMismatches = 0; //!< 接地が食い違ったフレーム数
        std::size_t firstDivergedStep = 0;  //!< 最初に許容を外れたフレーム。無ければ共通のフレーム数
        bool matched = false;               //!< 許容の内側に収まったか
    };

    //! @brief 基準と実測を 1 フレームずつ突き合わせる
    //! @details フレーム数が違う時は共通の範囲だけ比べる。フレーム数そのものの食い違いは matched が落とす
    //! @param[in] baseline 基準の記録
    //! @param[in] actual 実測の記録
    //! @param[in] tolerance 許す差
    //! @return 差の最大と最初にずれたフレーム
    [[nodiscard]] inline TraceDiff CompareTraces(const std::vector<StepRecord>& baseline,
                                                 const std::vector<StepRecord>& actual,
                                                 const TraceTolerance& tolerance) noexcept
    {
        TraceDiff diff;
        diff.baselineSteps = baseline.size();
        diff.actualSteps = actual.size();

        const std::size_t common = std::min(baseline.size(), actual.size());
        diff.firstDivergedStep = common;

        for (std::size_t i = 0; i < common; ++i)
        {
            const float positionDelta = (actual[i].position - baseline[i].position).Length();
            const float velocityDelta = (actual[i].velocity - baseline[i].velocity).Length();
            diff.maxPositionDelta = std::max(diff.maxPositionDelta, positionDelta);
            diff.maxVelocityDelta = std::max(diff.maxVelocityDelta, velocityDelta);

            if (actual[i].grounded != baseline[i].grounded)
                ++diff.groundedMismatches;

            const bool insideTolerance = positionDelta <= tolerance.position && velocityDelta <= tolerance.velocity &&
                                         actual[i].grounded == baseline[i].grounded;
            if (!insideTolerance && diff.firstDivergedStep == common)
                diff.firstDivergedStep = i;
        }

        diff.matched = baseline.size() == actual.size() && diff.maxPositionDelta <= tolerance.position &&
                       diff.maxVelocityDelta <= tolerance.velocity &&
                       diff.groundedMismatches <= tolerance.groundedMismatches;
        return diff;
    }

    //! @brief 差の要約と、最初にずれたフレームの前後を文字列にする
    //! @details 並べるのはずれたフレームの前後 2 つずつの位置だけ。ずれが無ければ要約で終わる
    //! @param[in] diff CompareTraces の結果
    //! @param[in] baseline 基準の記録
    //! @param[in] actual 実測の記録
    //! @return 落ちた検証へ添える説明
    [[nodiscard]] inline std::string DescribeDiff(const TraceDiff& diff,
                                                  const std::vector<StepRecord>& baseline,
                                                  const std::vector<StepRecord>& actual)
    {
        std::ostringstream out;
        out << "フレーム数 基準 " << diff.baselineSteps << " / 実測 " << diff.actualSteps << "\n";
        out << "位置差の最大 " << diff.maxPositionDelta << " m\n";
        out << "速度差の最大 " << diff.maxVelocityDelta << "\n";
        out << "接地の食い違い " << diff.groundedMismatches << " フレーム\n";

        const std::size_t common = std::min(baseline.size(), actual.size());
        if (diff.firstDivergedStep >= common)
            return out.str();

        out << "最初にずれたフレーム " << diff.firstDivergedStep << "\n";

        std::size_t first = 0;
        if (diff.firstDivergedStep > 2)
            first = diff.firstDivergedStep - 2;
        const std::size_t last = std::min(common, diff.firstDivergedStep + 3);

        for (std::size_t i = first; i < last; ++i)
        {
            out << "  step " << i << " 基準 " << baseline[i].position.x << " " << baseline[i].position.y << " "
                << baseline[i].position.z << " / 実測 " << actual[i].position.x << " " << actual[i].position.y << " "
                << actual[i].position.z << "\n";
        }
        return out.str();
    }

    //! @brief 基準ファイルへ書く本文を作る
    //! @details 桁は max_digits10 で出す。float が読み直しで 1 ビットでも変わると、差 0 の比較が毎回落ちる
    //! @param[in] trace 毎固定ステップの記録
    //! @return 先頭が列名の行、以降は 1 行 1 フレームの本文
    [[nodiscard]] inline std::string FormatTrace(const std::vector<StepRecord>& trace)
    {
        std::ostringstream out;
        out << "# step posX posY posZ velX velY velZ grounded\n";
        out << std::setprecision(std::numeric_limits<float>::max_digits10);

        for (std::size_t i = 0; i < trace.size(); ++i)
        {
            const StepRecord& s = trace[i];
            int groundedFlag = 0;
            if (s.grounded)
                groundedFlag = 1;

            out << i << " " << s.position.x << " " << s.position.y << " " << s.position.z << " " << s.velocity.x << " "
                << s.velocity.y << " " << s.velocity.z << " " << groundedFlag << "\n";
        }
        return out.str();
    }

    //! @brief FormatTrace が書いた本文を記録の並びへ戻す
    //! @details 空行と # で始まる行は読み飛ばす
    //! @param[in] text 基準ファイルの本文
    //! @return 記録の並び。列が 1 つでも読めなければ std::nullopt
    [[nodiscard]] inline std::optional<std::vector<StepRecord>> ParseTrace(const std::string& text)
    {
        std::vector<StepRecord> trace;
        std::istringstream in(text);
        std::string line;

        while (std::getline(in, line))
        {
            if (line.empty() || line.front() == '#')
                continue;

            std::istringstream fields(line);
            std::size_t index = 0;
            StepRecord record;
            int groundedFlag = 0;
            fields >> index >> record.position.x >> record.position.y >> record.position.z >> record.velocity.x >>
                record.velocity.y >> record.velocity.z >> groundedFlag;
            if (!fields)
                return std::nullopt;

            record.grounded = groundedFlag != 0;
            trace.push_back(record);
        }
        return trace;
    }

    //! @brief 基準ファイルの場所を組む
    //! @param[in] name 基準の名前
    //! @return ContentRoot 下の Source/Tests/data/golden へ name.txt を足したパス
    [[nodiscard]] inline std::filesystem::path BaselinePath(const std::string& name)
    {
        return NS::Core::FileSystem::ContentRoot() / "Source" / "Tests" / "data" / "golden" / (name + ".txt");
    }

    //! @brief 基準ファイルを読む
    //! @param[in] name 基準の名前
    //! @return 記録の並び。ファイルが無いか本文が読めなければ std::nullopt
    [[nodiscard]] inline std::optional<std::vector<StepRecord>> LoadBaseline(const std::string& name)
    {
        const std::optional<std::string> text = NS::Core::FileSystem::ReadAllText(BaselinePath(name));
        if (!text.has_value())
            return std::nullopt;

        return ParseTrace(*text);
    }

    //! @brief 基準ファイルが無い時に添える案内を作る
    //! @param[in] name 基準の名前
    //! @return 探した場所と、基準を作り直すコマンド
    [[nodiscard]] inline std::string MissingBaselineMessage(const std::string& name)
    {
        return "基準ファイルがありません: " + BaselinePath(name).string() +
               "\n生成: $env:GTEST_ALSO_RUN_DISABLED_TESTS='1'; cmd /c Tools\\@run_tests.cmd nobuild Debug "
               "*Golden.DISABLED_SaveBaselines";
    }

    //! @brief 記録の並びを基準ファイルへ書く
    //! @details 呼ぶのは DISABLED_SaveBaselines だけ。通常の実行で書くと基準が実測に追い付き、差が出なくなる
    //! @param[in] name 基準の名前
    //! @param[in] trace 毎固定ステップの記録
    //! @return 書けた場合 true、それ以外の場合は false
    inline bool SaveBaseline(const std::string& name, const std::vector<StepRecord>& trace)
    {
        const std::string text = FormatTrace(trace);
        const std::span<const char> chars{text.data(), text.size()};
        return NS::Core::FileSystem::WriteAllBytes(BaselinePath(name), std::as_bytes(chars));
    }
} // namespace NS::Tests

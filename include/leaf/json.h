#pragma once

#include <leaf/config.h>
#include <leaf/error.h>
#include <leaf/outcome.h>
#include <leaf/result.h>
#include <leaf/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace leaf {

Outcome<AnalyzerConfig> parseAnalyzerConfigJson(std::string_view utf8) noexcept;
Outcome<std::string> analysisResultToJson(const AnalysisResult& result) noexcept;
Outcome<std::string> batchResultToJson(const BatchResult& result) noexcept;
Outcome<std::string> errorToJson(const Error& error,
                                 const Versions& versions,
                                 std::optional<std::uint32_t> cAbiVersion = {}) noexcept;

}  // namespace leaf

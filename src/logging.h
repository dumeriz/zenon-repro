#pragma once

#include <fmt/format.h>

#ifdef NDEBUG
#include <systemd/sd-journal.h>
#else
#include <fmt/color.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

static plog::ColorConsoleAppender<plog::TxtFormatter> logger;
#endif



namespace logging
{
#ifdef NDEBUG // logging via journald in Release mode

    inline auto init() {}

#define log_error(fmt_str, ...) sd_journal_print(LOG_ERR, "%s", std::format(fmt_str, __VA_ARGS__).data());
#define log_warning(fmt_str, ...) sd_journal_print(LOG_WARNING, "%s", std::format(fmt_str, __VA_ARGS__).data());
#define log_debug(fmt_str, ...);
#define log_info(fmt_str, ...) sd_journal_print(LOG_INFO, "%s", std::format(fmt_str, __VA_ARGS__).data());

#else // logging via plog in Debug mode

    inline auto init() { plog::init(plog::debug, &logger); }

#define log_error(fmt_str, ...) PLOG_ERROR << fmt::format(fmt::fg(fmt::color::red), fmt_str, __VA_ARGS__);
#define log_warning(fmt_str, ...) PLOG_WARNING << fmt::format(fmt::fg(fmt::color::yellow), fmt_str, __VA_ARGS__);
#define log_debug(fmt_str, ...) PLOG_DEBUG << fmt::format(fmt::fg(fmt::color::blue), fmt_str, __VA_ARGS__);
#define log_info(fmt_str, ...) PLOG_INFO << fmt::format(fmt::runtime(fmt_str), __VA_ARGS__);

#endif
} // namespace logging

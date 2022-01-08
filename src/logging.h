#pragma once

#ifdef NDEBUG
#include <systemd/sd-journal.h>
#else
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

#ifdef NDEBUG
#include <format>
#else
#include <fmt/color.h>
#include <fmt/format.h>
#endif

static plog::ColorConsoleAppender<plog::TxtFormatter> logger;
#endif

namespace logging
{
#ifdef NDEBUG // logging via journald in Release mode

    inline auto init() {}

    template <typename... Args> auto error(std::string_view fmt, Args&&... args)
    {
        sd_journal_print(LOG_ERR, "%s", std::format(fmt, std::make_format_args(args...)).data());
    }

    template <typename... Args> auto warning(std::string_view fmt, Args&&... args)
    {
        sd_journal_print(LOG_WARNING, "%s", std::format(fmt, std::make_format_args(args...)).data());
    }

    template <typename... Args> auto debug(Args&&...) {}

    template <typename... Args> auto info(std::string_view fmt, Args&&...)
    {
        sd_journal_print(LOG_INFO, "%s", std::format(fmt, std::make_format_args(args...)).data());
    }

#else // logging via plog in Debug mode

    inline auto init() { plog::init(plog::debug, &logger); }

    template <typename... Args> auto error(std::string_view fmt_str, Args&&... args)
    {
        PLOG_ERROR << fmt::format(fmt::fg(fmt::color::red), fmt_str, args...);
    }

    template <typename... Args> auto warning(std::string_view fmt_str, Args&&... args)
    {
        PLOG_WARNING << fmt::format(fmt::fg(fmt::color::yellow), fmt_str, args...);
    }

    template <typename... Args> auto debug(std::string_view fmt_str, Args&&... args)
    {
        PLOG_DEBUG << fmt::format(fmt::fg(fmt::color::blue), fmt_str, args...);
    }

    template <typename... Args> auto info(std::string_view fmt_str, Args&&... args)
    {
        PLOG_INFO << fmt::format(fmt::fg(fmt::color::green), fmt_str, args...);
    }

#endif
} // namespace logging

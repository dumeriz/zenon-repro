#pragma once

#ifdef NDEBUG
#include <systemd/sd-journal.h>
#else
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

static plog::ColorConsoleAppender<plog::TxtFormatter> logger;
#endif

#include <sstream>

namespace logging::detail
{
    template <typename... Args> auto stream_args(Args&&... args)
    {
        std::ostringstream oss;
        (oss << ... << args);
        return oss.str();
    }
} // namespace logging::detail

namespace logging
{
#ifdef NDEBUG // logging via journald in Release mode

    inline auto init() {}

    template <typename... Args> auto error(Args&&... args)
    {
        sd_journal_print(LOG_ERR, "%s", detail::stream_args(std::forward<Args>(args)...).data());
    }

    template <typename... Args> auto warning(Args&&... args)
    {
        sd_journal_print(LOG_WARNING, "%s", detail::stream_args(std::forward<Args>(args)...).data());
    }

    template <typename... Args> auto debug(Args&&...) {}

    template <typename... Args> auto info(Args&&...)
    {
        sd_journal_print(LOG_INFO, "%s", detail::stream_args(std::forward<Args>(args)...).data());
    }

#else // logging via plog in Debug mode

    inline auto init() { plog::init(plog::debug, &logger); }

    template <typename... Args> auto error(Args&&... args)
    {
        PLOG_ERROR << detail::stream_args(std::forward<Args>(args)...);
    }

    template <typename... Args> auto warning(Args&&... args)
    {
        PLOG_WARNING << detail::stream_args(std::forward<Args>(args)...);
    }

    template <typename... Args> auto debug(Args&&... args)
    {
        PLOG_DEBUG << detail::stream_args(std::forward<Args>(args)...);
    }

    template <typename... Args> auto info(Args&&... args)
    {
        PLOG_INFO << detail::stream_args(std::forward<Args>(args)...);
    }

#endif
} // namespace logging

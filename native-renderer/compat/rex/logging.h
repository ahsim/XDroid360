// XDroid360: ReXGlue's logging macros (rex/logging.h) onto Xenia's (xenia/base/logging.h), which
// go to logcat and, through the runtime's sinks, the game's log.
#pragma once

#include "xenia/base/logging.h"

#define REXLOG_TRACE(...) XELOGD(__VA_ARGS__)
#define REXLOG_DEBUG(...) XELOGD(__VA_ARGS__)
#define REXLOG_INFO(...) XELOGI(__VA_ARGS__)
#define REXLOG_WARN(...) XELOGW(__VA_ARGS__)
#define REXLOG_ERROR(...) XELOGE(__VA_ARGS__)
#define REXLOG_CRITICAL(...) XELOGE(__VA_ARGS__)

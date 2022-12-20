#include "log/log.h"

#include <cstdarg>
#include <cstdio>
#include <iomanip>
#include <iostream>

#include <glog/logging.h>
#include <glog/raw_logging.h>

#define VLOG_IS_ON(verboselevel, file)                                \
  __extension__  \
  ({ static google::SiteFlag vlocal__ = {NULL, NULL, 0, NULL};       \
     google::int32 verbose_level__ = (verboselevel);                    \
     (vlocal__.level == NULL ? google::InitVLOG3__(&vlocal__, &FLAGS_v, \
                        file, verbose_level__) : *vlocal__.level >= verbose_level__); \
  })

static void CustomPrefix(std::ostream &s, const google::LogMessageInfo &l, void *) {
    s << '[' << l.severity << ']'
      << '[' << std::setw(2) << l.time.hour() << ':' << std::setw(2) << l.time.min() << ':' << std::setw(2) << l.time.sec() << ']'
      << '[' << l.filename << ':' << l.line_number << ']';
}

void log_init(const char *program) {
    FLAGS_logtostderr = true;

    google::InitGoogleLogging(program, &CustomPrefix);
}

void _log_raw(int severity, const char *file, int line, const char *fmt, ...) {
    va_list args1;
    va_start(args1, fmt);
    va_list args2;
    va_copy(args2, args1);
    std::vector<char> buf(1 + std::vsnprintf(nullptr, 0, fmt, args1));
    va_end(args1);
    std::vsnprintf(buf.data(), buf.size(), fmt, args2);
    va_end(args2);

    std::string message = std::string(buf.begin(), buf.end());
    google::LogMessage(file, line, severity).stream() << message;
}

void _log_raw_debug(int severity, const char *file, int line, const char *fmt, ...) {
#if DCHECK_IS_ON()
    va_list args1;
    va_start(args1, fmt);
    va_list args2;
    va_copy(args2, args1);
    std::vector<char> buf(1 + std::vsnprintf(nullptr, 0, fmt, args1));
    va_end(args1);
    std::vsnprintf(buf.data(), buf.size(), fmt, args2);
    va_end(args2);

    std::string message = std::string(buf.begin(), buf.end());
#endif
}

void _log_raw_verbose(int severity, const char *file, int line, const char *fmt, ...) {
    if (google::IsGoogleLoggingInitialized() && VLOG_IS_ON(severity, file)) {
        va_list args1;
        va_start(args1, fmt);
        va_list args2;
        va_copy(args2, args1);
        std::vector<char> buf(1 + std::vsnprintf(nullptr, 0, fmt, args1));
        va_end(args1);
        std::vsnprintf(buf.data(), buf.size(), fmt, args2);
        va_end(args2);

        std::string message = std::string(buf.begin(), buf.end());
    }
}
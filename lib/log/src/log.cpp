#include "log/log.h"

#include <cstdarg>
#include <cstdio>
#include <iomanip>
#include <iostream>

#include <glog/logging.h>
#include <glog/raw_logging.h>

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
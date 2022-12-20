#ifndef LOG_H_
#define LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_INFO    0
#define LOG_WARNING 1
#define LOG_ERROR   2
#define LOG_FATAL   3

#define LOGD(...) _log_raw_debug(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGV(...) _log_raw_verbose(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGI(...) _log_raw(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...) _log_raw(LOG_WARNING, __FILE__, __LINE__, __VA_ARGS__)
#define LOGE(...) _log_raw(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOGF(...) _log_raw(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

void log_init(const char *program);
void _log_raw(int severity, const char *file, int line, const char *fmt, ...);
void _log_raw_debug(int severity, const char *file, int line, const char *fmt, ...);
void _log_raw_verbose(int severity, const char *file, int line, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif

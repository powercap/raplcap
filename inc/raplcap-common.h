/**
 * Common utilities, like logging.
 *
 * @author Connor Imes
 * @date 2017-05-08
 */
#ifndef _RAPLCAP_COMMON_H_
#define _RAPLCAP_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <float.h>
#include <stdio.h>
#include <string.h>

typedef enum raplcap_loglevel {
  DEBUG = 0,
  INFO,
  WARN,
  ERROR,
  OFF,
} raplcap_loglevel;

#ifndef RAPLCAP_LOG_LEVEL
  #define RAPLCAP_LOG_LEVEL WARN
#endif

#ifndef RAPLCAP_IMPL
  #define RAPLCAP_IMPL "raplcap"
#endif

// Function-like macros allow log messages to be optimized out by the compiler

#define raplcap_is_log_enabled(severity) ((severity) >= RAPLCAP_LOG_LEVEL)

#define TO_FILE(severity) (severity) >= WARN ? stderr : stdout

#define TO_LOG_PREFIX(severity) \
  (severity) == DEBUG ? "[DEBUG]" : \
  (severity) == INFO  ? "[INFO] " : \
  (severity) == WARN  ? "[WARN] " : \
                        "[ERROR]"

#define raplcap_log(severity, ...) \
  do { if (raplcap_is_log_enabled((severity))) { \
      fprintf(TO_FILE((severity)), "%s [%s] ", TO_LOG_PREFIX((severity)), RAPLCAP_IMPL); \
      fprintf(TO_FILE((severity)), __VA_ARGS__); \
    } } while (0)


#define raplcap_perror(severity, msg) \
  raplcap_log(severity, "%s: %s\n", msg, strerror(errno))

/**
 * Returns 1 (true) if the value is close enough to zero.
 * Silences compiler warnings about comparing double values with "==".
 */
#define is_zero_dbl(val) ((val) >= 0 ? (val) < DBL_EPSILON : (val) > -DBL_EPSILON)

#ifdef __cplusplus
}
#endif

#endif

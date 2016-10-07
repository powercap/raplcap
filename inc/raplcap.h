/**
 * A power capping interface for Intel Running Average Power Limit (RAPL).
 *
 * @author Connor Imes
 * @date 2016-05-13
 */
#ifndef _RAPLCAP_H_
#define _RAPLCAP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct raplcap {
  uint32_t nsockets;
  void* state;
} raplcap;

typedef struct raplcap_limit {
  double seconds;
  double watts;
} raplcap_limit;

typedef enum raplcap_zone {
  RAPLCAP_ZONE_PACKAGE = 0,
  RAPLCAP_ZONE_CORE,
  RAPLCAP_ZONE_UNCORE,
  RAPLCAP_ZONE_DRAM,
  RAPLCAP_ZONE_PSYS,
} raplcap_zone;

/**
 * Initialize the RAPL powercap context
 */
int raplcap_init(raplcap* rc);

/**
 * Destroy the RAPL powercap context
 */
int raplcap_destroy(raplcap* rc);

/**
 * Get the number of available sockets.
 * If the context is NULL, the function will attempt to discover the number of available sockets.
 */
uint32_t raplcap_get_num_sockets(const raplcap* rc);

/**
 * Check if a zone is supported.
 * Returns a negative value on error, 0 if unsupported, 1 if supported.
 */
int raplcap_is_zone_supported(uint32_t socket, const raplcap* rc, raplcap_zone zone);

/**
 * Get the limits for a zone, if it is supported.
 * Not all zones use limit_short.
 * Returns a negative value on error, 0 on suceess.
 */
int raplcap_get_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short);

/**
 * Set the limits for a zone, if it is supported.
 * Not all zones uses limit_short.
 * If the power or time window value is 0, it will not be written or the current value may be used.
 * Returns a negative value on error, 0 on suceess.
 */
int raplcap_set_limits(uint32_t socket, const raplcap* rc, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short);

#ifdef __cplusplus
}
#endif

#endif

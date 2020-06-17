/**
 * Wrappers for older raplcap.h functions
 *
 * @author Connor Imes
 * @date 2020-06-12
 */
#ifndef _RAPLCAP_WRAPPERS_H_
#define _RAPLCAP_WRAPPERS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <raplcap.h>

uint32_t raplcap_get_num_sockets(const raplcap* rc) {
  return raplcap_get_num_packages(rc);
}

int raplcap_is_zone_supported(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_pd_is_zone_supported(rc, pkg, 0, zone);
}

int raplcap_is_zone_enabled(const raplcap* rc, uint32_t pkg, raplcap_zone zone){
  return raplcap_pd_is_zone_enabled(rc, pkg, 0, zone);
}

int raplcap_set_zone_enabled(const raplcap* rc, uint32_t pkg, raplcap_zone zone, int enabled) {
  return raplcap_pd_set_zone_enabled(rc, pkg, 0, zone, enabled);
}

int raplcap_get_limits(const raplcap* rc, uint32_t pkg, raplcap_zone zone,
                       raplcap_limit* limit_long, raplcap_limit* limit_short) {
  return raplcap_pd_get_limits(rc, pkg, 0, zone, limit_long, limit_short);
}

int raplcap_set_limits(const raplcap* rc, uint32_t pkg, raplcap_zone zone,
                       const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  return raplcap_pd_set_limits(rc, pkg, 0, zone, limit_long, limit_short);
}

double raplcap_get_energy_counter(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_pd_get_energy_counter(rc, pkg, 0, zone);
}

double raplcap_get_energy_counter_max(const raplcap* rc, uint32_t pkg, raplcap_zone zone) {
  return raplcap_pd_get_energy_counter_max(rc, pkg, 0, zone);
}

#ifdef __cplusplus
}
#endif

#endif

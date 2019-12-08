/**
 * Functionality specific to raplcap-msr.
 * Units generally indicate the precision at which relevant values can be read or written.
 *
 * @author Connor Imes
 * @date 2018-05-19
 */
#ifndef _RAPLCAP_MSR_H_
#define _RAPLCAP_MSR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <raplcap.h>

/**
 * Check if a zone is locked.
 *
 * @param rc
 * @param socket
 * @param zone
 * @return 0 if unlocked, 1 if locked, a negative value on error
 */
int raplcap_msr_is_zone_locked(const raplcap* rc, uint32_t socket, raplcap_zone zone);

/**
 * Check if a zone is clamping.
 *
 * @param rc
 * @param socket
 * @param zone
 * @return 0 if not clamping, 1 if clamping, a negative value on error
 */
int raplcap_msr_is_zone_clamping(const raplcap* rc, uint32_t socket, raplcap_zone zone);

/**
 * Set/unset zone clamping.
 * Note: clamping is automatically set when a zone is enabled.
 *
 * @param rc
 * @param socket
 * @param zone
 * @param clamping
 * @return 0 on success, a negative value on error
 */
int raplcap_msr_set_zone_clamping(const raplcap* rc, uint32_t socket, raplcap_zone zone, int clamping);

/**
 * Get the time units for a zone in seconds.
 *
 * @param rc
 * @param socket
 * @param zone
 * @return Seconds on success, a negative value on error
 */
double raplcap_msr_get_time_units(const raplcap* rc, uint32_t socket, raplcap_zone zone);

/**
 * Get the power units for a zone in Watts.
 *
 * @param rc
 * @param socket
 * @param zone
 * @return Watts on success, a negative value on error
 */
double raplcap_msr_get_power_units(const raplcap* rc, uint32_t socket, raplcap_zone zone);

/**
 * Get the energy units for a zone in Joules.
 *
 * @param rc
 * @param socket
 * @param zone
 * @return Joules on success, a negative value on error
 */
double raplcap_msr_get_energy_units(const raplcap* rc, uint32_t socket, raplcap_zone zone);

#ifdef __cplusplus
}
#endif

#endif

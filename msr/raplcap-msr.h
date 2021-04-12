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
 * Check if a zone is clamped.
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @return 0 if not clamped, 1 if clamped, a negative value on error
 */
int raplcap_msr_pd_is_zone_clamped(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone);

/**
 * Clamp/unclamp a zone.
 * Note: clamping is automatically set when a zone is enabled.
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @param clamp
 * @return 0 on success, a negative value on error
 */
int raplcap_msr_pd_set_zone_clamped(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone, int clamped);

/**
 * Check if a zone is locked.
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @return 0 if unlocked, 1 if locked, a negative value on error
 */
int raplcap_msr_pd_is_zone_locked(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone);

/**
 * Lock a zone.
 * Note: once locked, a zone cannot be unlocked until CPU is reset.
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @return 0 on success, a negative value on error
 */
int raplcap_msr_pd_set_zone_locked(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone);

/**
 * Check if a constraint is locked (affects other constraints that share a MSR).
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @param constraint
 * @return 0 if unlocked, 1 if locked, a negative value on error
 */
int raplcap_msr_pd_is_locked(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                             raplcap_constraint constraint);

/**
 * Lock a constraint (affects other constraints that share a MSR)
 * Note: once locked, a constraint cannot be unlocked until CPU is reset.
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @param constraint
 * @return 0 on success, a negative value on error
 */
int raplcap_msr_pd_set_locked(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone,
                              raplcap_constraint constraint);

/**
 * Get the time units for a zone in seconds.
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @return Seconds on success, a negative value on error
 */
double raplcap_msr_pd_get_time_units(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone);

/**
 * Get the power units for a zone in Watts.
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @return Watts on success, a negative value on error
 */
double raplcap_msr_pd_get_power_units(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone);

/**
 * Get the energy units for a zone in Joules.
 *
 * @param rc
 * @param pkg
 * @param die
 * @param zone
 * @return Joules on success, a negative value on error
 */
double raplcap_msr_pd_get_energy_units(const raplcap* rc, uint32_t pkg, uint32_t die, raplcap_zone zone);

/**
 * Assumes die=0.
 *
 * @deprecated
 * @see raplcap_msr_pd_is_zone_clamped
 */
int raplcap_msr_is_zone_clamped(const raplcap* rc, uint32_t pkg, raplcap_zone zone);

/**
 * Assumes die=0.
 *
 * @deprecated
 * @see raplcap_msr_pd_set_zone_clamped
 */
int raplcap_msr_set_zone_clamped(const raplcap* rc, uint32_t pkg, raplcap_zone zone, int clamped);

/**
 * Assumes die=0.
 *
 * @deprecated
 * @see raplcap_msr_pd_is_zone_locked
 */
int raplcap_msr_is_zone_locked(const raplcap* rc, uint32_t pkg, raplcap_zone zone);

/**
 * Assumes die=0.
 *
 * @deprecated
 * @see raplcap_msr_pd_set_zone_locked
 */
int raplcap_msr_set_zone_locked(const raplcap* rc, uint32_t pkg, raplcap_zone zone);

/**
 * Assumes die=0.
 *
 * @deprecated
 * @see raplcap_msr_pd_get_time_units
 */
double raplcap_msr_get_time_units(const raplcap* rc, uint32_t pkg, raplcap_zone zone);

/**
 * Assumes die=0.
 *
 * @deprecated
 * @see raplcap_msr_pd_get_power_units
 */
double raplcap_msr_get_power_units(const raplcap* rc, uint32_t pkg, raplcap_zone zone);

/**
 * Assumes die=0.
 *
 * @deprecated
 * @see raplcap_msr_pd_get_energy_units
 */
double raplcap_msr_get_energy_units(const raplcap* rc, uint32_t pkg, raplcap_zone zone);

#ifdef __cplusplus
}
#endif

#endif

#ifndef _RAPLCAP_MSR_COMMON_H_
#define _RAPLCAP_MSR_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include "raplcap.h"

#pragma GCC visibility push(hidden)

#define MSR_RAPL_POWER_UNIT       0x606
/* Package RAPL Domain */
#define MSR_PKG_POWER_LIMIT       0x610
#define MSR_PKG_ENERGY_STATUS     0x611
/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT       0x638
#define MSR_PP0_ENERGY_STATUS     0x639
/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT       0x640
#define MSR_PP1_ENERGY_STATUS     0x641
/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT      0x618
#define MSR_DRAM_ENERGY_STATUS    0x619
/* Platform (PSys) Domain (Skylake and newer) */
#define MSR_PLATFORM_POWER_LIMIT  0x65C
#define MSR_PLATFORM_ENERGY_COUNTER 0x64D
/* PL4 Power Limit (Tiger Lake and newer) */
#define MSR_VR_CURRENT_CONFIG     0x601

#define RAPLCAP_NZONES (RAPLCAP_ZONE_PSYS + 1)

typedef uint64_t (fn_to_msr) (double value, double units);
typedef double (fn_from_msr) (uint64_t bits, double units);

typedef struct raplcap_msr_zone_cfg {
  fn_to_msr* to_msr_tw;
  fn_from_msr* from_msr_tw;
  fn_to_msr* to_msr_pl;
  fn_from_msr* from_msr_pl;
  uint8_t constraints;
} raplcap_msr_zone_cfg;

typedef struct raplcap_msr_ctx {
  const raplcap_msr_zone_cfg* cfg;
  double power_units;
  double energy_units;
  double energy_units_dram;
  double time_units;
  uint32_t cpu_model;
} raplcap_msr_ctx;

/**
 * Get the CPU model, or return 0 if the model isn't supported.
 */
uint32_t msr_get_supported_cpu_model(void);

/**
 * Populate the context.
 */
void msr_get_context(raplcap_msr_ctx* ctx, uint32_t cpu_model, uint64_t units_msrval);

/**
 * Parse msrval to determine if zone is enabled.
 */
int msr_is_zone_enabled(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                        int* en_long, int* en_short);

/**
 * Set bit fields on msrval to enable/disable zone. Returns modified msrval.
 */
uint64_t msr_set_zone_enabled(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                              const int* en_long, const int* en_short);

/**
 * Parse msrval to determine if zone is clamped.
 */
int msr_is_zone_clamped(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                        int* cl_long, int* cl_short);

/**
 * Set bit fields on msrval to clamp/unclamp zone. Returns modified msrval.
 */
uint64_t msr_set_zone_clamped(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                              const int* cl_long, const int* cl_short);

/**
 * Parse msrval to determine if zone is locked.
 */
int msr_is_zone_locked(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval);

/**
 * Set bit fields on msrval to lock/unlock a zone (in practice a zone can't be unlocked). Returns modified msrval.
 */
uint64_t msr_set_zone_locked(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval, int locked);

/**
 * Parse msrval and translate bit fields to populate limits.
 */
void msr_get_limits(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                    raplcap_limit* limit_long, raplcap_limit* limit_short);

/**
 * Set bit fields on msrval based on limit values > 0.
 */
uint64_t msr_set_limits(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                        const raplcap_limit* limit_long, const raplcap_limit* limit_short);

/**
 * Get the energy counter value in Joules.
 */
double msr_get_energy_counter(const raplcap_msr_ctx* ctx, uint64_t msrval, raplcap_zone zone);

/**
 * Get the max energy counter value in Joules.
 */
double msr_get_energy_counter_max(const raplcap_msr_ctx* ctx, raplcap_zone zone);

/**
 * Get the time units in seconds.
 */
double msr_get_time_units(const raplcap_msr_ctx* ctx, raplcap_zone zone);

/**
 * Get the power units in Watts.
 */
double msr_get_power_units(const raplcap_msr_ctx* ctx, raplcap_zone zone);

/**
 * Get the energy units in Joules.
 */
double msr_get_energy_units(const raplcap_msr_ctx* ctx, raplcap_zone zone);

#pragma GCC visibility pop

#ifdef __cplusplus
}
#endif

#endif

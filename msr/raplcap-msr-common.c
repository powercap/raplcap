/**
 * Common MSR functions, mostly for translating to/from bit fields.
 *
 * @author Connor Imes
 * @date 2017-12-16
 */
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include "raplcap-common.h"
#include "raplcap-cpuid.h"
#include "raplcap-msr-common.h"

#define HAS_SHORT_TERM(ctx, zone) (ctx->cfg[zone].constraints > 1)

#define PU_MASK   0xF
#define PU_SHIFT  0
#define EU_MASK   0x1F
#define EU_SHIFT  8
#define TU_MASK   0xF
#define TU_SHIFT  16
#define PL_MASK   0x7FFF
#define PL1_SHIFT 0
#define PL2_SHIFT 32
#define TL_MASK   0x7F
#define TL1_SHIFT 17
#define TL2_SHIFT 49
#define EN_MASK   0x1
#define EN1_SHIFT 15
#define EN2_SHIFT 47
#define CL_MASK   0x1
#define CL1_SHIFT 16
#define CL2_SHIFT 48
#define LCK_MASK  0x1
#define EY_MASK   0xFFFFFFFF
#define EY_SHIFT  0

// 2^y
static uint64_t pow2_u64(uint64_t y) {
  return ((uint64_t) 1) << y;
}

// log2(y); returns 0 for y = 0
static uint64_t log2_u64(uint64_t y) {
  uint64_t ret = 0;
  while (y >>= 1) {
    ret++;
  }
  return ret;
}

// Section 14.9.1
static double from_msr_pu_default(uint64_t msrval) {
  return 1.0 / pow2_u64((msrval >> PU_SHIFT) & PU_MASK);
}

// Table 2-8
static double from_msr_pu_atom(uint64_t msrval) {
  return pow2_u64((msrval >> PU_SHIFT) & PU_MASK) / 1000.0;
}

// Section 14.9.1
static double from_msr_eu_default(uint64_t msrval) {
  return 1.0 / pow2_u64((msrval >> EU_SHIFT) & EU_MASK);
}

// Table 2-8
static double from_msr_eu_atom(uint64_t msrval) {
  return pow2_u64((msrval >> EU_SHIFT) & EU_MASK) / 1000000.0;
}

// Section 14.9.1
static double from_msr_tu_default(uint64_t msrval) {
  // For Atom, Table 2-8 specifies that field value is always 0x0, meaning 1 second, so this works still
  return 1.0 / pow2_u64((msrval >> TU_SHIFT) & TU_MASK);
}

// Section 14.9.1
static double from_msr_pl_default(uint64_t bits, double power_units) {
  assert(power_units > 0);
  const double watts = power_units * bits;
  raplcap_log(DEBUG, "from_msr_pl_default: bits=%04lX, power_units=%.12f, watts=%.12f\n", bits, power_units, watts);
  return watts;
}

// Section 14.9.1
static uint64_t to_msr_pl_default(double watts, double power_units) {
  assert(watts >= 0);
  assert(power_units > 0);
  // Lower bound is 0, but upper bound is limited by what fits in 15 bits
  static const uint64_t MSR_POWER_MAX = 0x7FFF;
  uint64_t bits = (uint64_t) (watts / power_units);
  if (bits > MSR_POWER_MAX) {
    raplcap_log(WARN, "Power limit too large: %.12f W, using max: %.12f W\n", watts, MSR_POWER_MAX * power_units);
    bits = MSR_POWER_MAX;
  }
  raplcap_log(DEBUG, "to_msr_pl_default: watts=%.12f, power_units=%.12f, bits=0x%04lX\n", watts, power_units, bits);
  return bits;
}

/**
 * Note: Intel's documentation (Section 14.9.3) specifies different conversions for Package and Power Planes.
 * We use the Package equation for Power Planes as well, which the Linux kernel appears to agree with.
 * Time window (seconds) = 2^Y * (1 + F/4) * Time_Unit
 * See the Linux kernel: drivers/powercap/intel_rapl.c:rapl_compute_time_window_core
 */
// Section 14.9.3
static double from_msr_tw_default(uint64_t bits, double time_units) {
  assert(time_units > 0);
  // "Y" is an unsigned integer value represented by lower 5 bits
  // "F" is an unsigned integer value represented by upper 2 bits
  const uint64_t y = bits & 0x1F;
  const uint64_t f = (bits >> 5) & 0x3;
  const double seconds = pow2_u64(y) * ((4 + f) / 4.0) * time_units;
  raplcap_log(DEBUG, "from_msr_tw_default: bits=0x%02lX, time_units=%.12f, y=0x%02lX, f=0x%lX, seconds=%.12f\n",
              bits, time_units, y, f, seconds);
  return seconds;
}

// Section 14.9.3
static uint64_t to_msr_tw_default(double seconds, double time_units) {
  assert(seconds > 0);
  assert(time_units > 0);
  // Seconds cannot be shorter than the smallest time unit - log2 would get a negative value and overflow "y".
  // They also cannot be larger than 2^2^5-1 so that log2 doesn't produce a value that uses more than 5 bits for "y".
  // Clamping prevents values outside the allowable range, but precision can still be lost in the conversion.
  static const double MSR_TIME_MIN = 1.0;
  static const double MSR_TIME_MAX = (double) 0xFFFFFFFF;
  double t = seconds / time_units;
  if (t < MSR_TIME_MIN) {
    raplcap_log(WARN, "Time window too small: %.12f sec, using min: %.12f sec\n", seconds, MSR_TIME_MIN * time_units);
    t = MSR_TIME_MIN;
  } else if (t > MSR_TIME_MAX) {
    // "trying" instead of "using" because precision loss will definitely throw off the final value at this extreme
    raplcap_log(WARN, "Time window too large: %.12f sec, trying max: %.12f sec\n", seconds, MSR_TIME_MAX * time_units);
    t = MSR_TIME_MAX;
  }
  // y = log2((4*t)/(4+f)); we can ignore "f" since t >= 1 and 0 <= f <= 3; we can also drop the real part of "t"
  const uint64_t y = log2_u64((uint64_t) t);
  // f = (4*t)/(2^y)-4; the real part of "t" only matters for t < 4, otherwise it's insignificant in computing "f"
  const uint64_t f = (((uint64_t) (4 * t)) / pow2_u64(y)) - 4;
  const uint64_t bits = ((y & 0x1F) | ((f & 0x3) << 5));
  raplcap_log(DEBUG, "to_msr_tw_default: seconds=%.12f, time_units=%.12f, t=%.12f, y=0x%02lX, f=0x%lX, bits=0x%02lX\n",
              seconds, time_units, t, y, f, bits);
  return bits;
}

// Table 2-8
static double from_msr_tw_atom(uint64_t bits, double time_units) {
  assert(time_units > 0);
  // If 0 is specified in bits [23:17], defaults to 1 second window, which should be the same as time_units.
  const double seconds = bits ? (bits * time_units) : time_units;
  raplcap_log(DEBUG, "from_msr_tw_atom: bits=0x%02lX, seconds=%.12f\n", bits, seconds);
  return seconds;
}

// Table 2-8
static uint64_t to_msr_tw_atom(double seconds, double time_units) {
  assert(seconds > 0);
  assert(time_units > 0);
  // time_units should be 1.0, but conceivably could be any whole number in 4 bit range: [1, 15]
  static const uint64_t MSR_TIME_MAX = 0x7F;
  const double t = seconds / time_units;
  uint64_t bits;
  if (seconds < 1) {
    raplcap_log(WARN, "Time window too small: %.12f sec, using min: %.12f sec\n", seconds, 1.0);
    bits = 0x0; // interpreted as 1 second
  } else if (t > (double) MSR_TIME_MAX) {
    raplcap_log(WARN, "Time window too large: %.12f sec, using max: %.12f sec\n", seconds, MSR_TIME_MAX * time_units);
    bits = MSR_TIME_MAX;
  } else {
    // round to nearest MSR value ((s + u/2) / u)
    bits = (uint64_t) (t + 0.5);
  }
  raplcap_log(DEBUG, "to_msr_tw_atom: seconds=%.12f, bits=0x%02lX\n", seconds, bits);
  return bits;
}

// Table 2-11
static double from_msr_tw_atom_airmont(uint64_t bits, double time_units) {
  // Used only for Airmont PP0 (CORE) zone
  (void) time_units;
  // If 0 is specified in bits [23:17], defaults to 1 second window.
  const double seconds = bits ? bits * 5.0 : 1.0;
  raplcap_log(DEBUG, "from_msr_tw_atom_airmont: bits=0x%02lX, seconds=%.12f\n", bits, seconds);
  return seconds;
}

// Table 2-11
static uint64_t to_msr_tw_atom_airmont(double seconds, double time_units) {
  // Used only for Airmont PP0 (CORE) zone
  (void) time_units;
  assert(seconds > 0);
  static const uint64_t MSR_TIME_MIN = 0x0; // 1 second
  static const uint64_t MSR_TIME_MAX = 0xA; // 50 seconds
  uint64_t bits;
  if (seconds < 1) {
    raplcap_log(WARN, "Time window too small: %.12f sec, using min: 1 sec\n", seconds);
    bits = MSR_TIME_MIN;
  } else if (seconds > 50) {
    raplcap_log(WARN, "Time window too large: %.12f sec, using max: 50 sec\n", seconds);
    bits = MSR_TIME_MAX;
  } else {
    // round to nearest multiple of 5
    bits = (uint64_t) ((seconds / 5.0) + 0.5);
  }
  raplcap_log(DEBUG, "to_msr_tw_atom_airmont: seconds=%.12f, time_units=%.12f, bits=0x%02lX\n",
              seconds, time_units, bits);
  return bits;
}

#define CFG_STATIC_INIT(ttw, ftw, tpl, fpl, c) { \
  .to_msr_tw = ttw, \
  .from_msr_tw = ftw, \
  .to_msr_pl = tpl, \
  .from_msr_pl = fpl, \
  .constraints = c }

static const raplcap_msr_zone_cfg CFG_DEFAULT[RAPLCAP_NZONES] = {
  CFG_STATIC_INIT(to_msr_tw_default, from_msr_tw_default, to_msr_pl_default, from_msr_pl_default, 2), // PACKAGE
  CFG_STATIC_INIT(to_msr_tw_default, from_msr_tw_default, to_msr_pl_default, from_msr_pl_default, 1), // CORE
  CFG_STATIC_INIT(to_msr_tw_default, from_msr_tw_default, to_msr_pl_default, from_msr_pl_default, 1), // UNCORE
  CFG_STATIC_INIT(to_msr_tw_default, from_msr_tw_default, to_msr_pl_default, from_msr_pl_default, 1), // DRAM
  CFG_STATIC_INIT(to_msr_tw_default, from_msr_tw_default, to_msr_pl_default, from_msr_pl_default, 2)  // PSYS
};

static const raplcap_msr_zone_cfg CFG_ATOM[RAPLCAP_NZONES] = {
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 1), // PACKAGE
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 1), // CORE
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 1), // UNCORE
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 1), // DRAM
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 2), // PSYS
};

// only the CORE time window is different from other ATOM CPUs
static const raplcap_msr_zone_cfg CFG_ATOM_AIRMONT[RAPLCAP_NZONES] = {
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 1), // PACKAGE
  CFG_STATIC_INIT(to_msr_tw_atom_airmont, from_msr_tw_atom_airmont, to_msr_pl_default, from_msr_pl_default, 1), // CORE
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 1), // UNCORE
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 1), // DRAM
  CFG_STATIC_INIT(to_msr_tw_atom, from_msr_tw_atom, to_msr_pl_default, from_msr_pl_default, 2), // PSYS
};

uint32_t msr_get_supported_cpu_model(void) {
  uint32_t cpu_family;
  uint32_t cpu_model;
  cpuid_get_family_model(&cpu_family, &cpu_model);
  if (!cpuid_is_vendor_intel() || !cpuid_is_cpu_supported(cpu_family, cpu_model)) {
    raplcap_log(ERROR, "CPU not supported: Family=%"PRIu32", Model=%02X\n", cpu_family, cpu_model);
    return 0;
  }
  return cpu_model;
}

void msr_get_context(raplcap_msr_ctx* ctx, uint32_t cpu_model, uint64_t units_msrval) {
  assert(ctx != NULL);
  assert(cpu_model > 0);
  ctx->cpu_model = cpu_model;
  switch (cpu_model) {
    case CPUID_MODEL_SANDYBRIDGE:
    case CPUID_MODEL_SANDYBRIDGE_X:
    //
    case CPUID_MODEL_IVYBRIDGE:
    case CPUID_MODEL_IVYBRIDGE_X:
    //
    case CPUID_MODEL_HASWELL:
    case CPUID_MODEL_HASWELL_L:
    case CPUID_MODEL_HASWELL_G:
    //
    case CPUID_MODEL_BROADWELL:
    case CPUID_MODEL_BROADWELL_G:
    //
    case CPUID_MODEL_SKYLAKE_L:
    case CPUID_MODEL_SKYLAKE:
    //
    case CPUID_MODEL_KABYLAKE_L:
    case CPUID_MODEL_KABYLAKE:
    //
    case CPUID_MODEL_CANNONLAKE_L:
    //
    case CPUID_MODEL_ICELAKE:
    case CPUID_MODEL_ICELAKE_L:
    //
    case CPUID_MODEL_COMETLAKE:
    case CPUID_MODEL_COMETLAKE_L:
    //
    case CPUID_MODEL_TIGERLAKE_L:
    case CPUID_MODEL_TIGERLAKE:
    //
    case CPUID_MODEL_ATOM_GOLDMONT:
    case CPUID_MODEL_ATOM_GOLDMONT_X:
    case CPUID_MODEL_ATOM_GOLDMONT_PLUS:
    case CPUID_MODEL_ATOM_TREMONT_D:
      ctx->power_units = from_msr_pu_default(units_msrval);
      ctx->energy_units = from_msr_eu_default(units_msrval);
      ctx->energy_units_dram = ctx->energy_units;
      ctx->time_units = from_msr_tu_default(units_msrval);
      ctx->cfg = CFG_DEFAULT;
      break;
    //----
    case CPUID_MODEL_HASWELL_X:
    case CPUID_MODEL_BROADWELL_X:
    case CPUID_MODEL_BROADWELL_D:
    case CPUID_MODEL_SKYLAKE_X:
    case CPUID_MODEL_ICELAKE_X:
    case CPUID_MODEL_ICELAKE_D:
    case CPUID_MODEL_XEON_PHI_KNL:
    case CPUID_MODEL_XEON_PHI_KNM:
      ctx->power_units = from_msr_pu_default(units_msrval);
      ctx->energy_units = from_msr_eu_default(units_msrval);
      ctx->energy_units_dram = 0.0000153;
      ctx->time_units = from_msr_tu_default(units_msrval);
      ctx->cfg = CFG_DEFAULT;
      break;
    //----
    case CPUID_MODEL_ATOM_SILVERMONT:
    case CPUID_MODEL_ATOM_SILVERMONT_MID:
    case CPUID_MODEL_ATOM_AIRMONT_MID:
    case CPUID_MODEL_ATOM_SOFIA:
      ctx->power_units = from_msr_pu_atom(units_msrval);
      ctx->energy_units = from_msr_eu_atom(units_msrval);
      ctx->energy_units_dram = ctx->energy_units;
      ctx->time_units = from_msr_tu_default(units_msrval);
      ctx->cfg = CFG_ATOM;
      break;
    //----
    case CPUID_MODEL_ATOM_AIRMONT:
      ctx->power_units = from_msr_pu_atom(units_msrval);
      ctx->energy_units = from_msr_eu_default(units_msrval);
      ctx->energy_units_dram = ctx->energy_units;
      ctx->time_units = from_msr_tu_default(units_msrval);
      ctx->cfg = CFG_ATOM_AIRMONT;
      break;
    //----
    default:
      raplcap_log(ERROR, "Unknown architecture\n");
      raplcap_log(ERROR, "Please report a bug if you see this message, it should never occur!\n");
      assert(0);
      break;
  }
  raplcap_log(DEBUG, "msr_get_context: model=%02X, "
              "power_units=%.12f, energy_units=%.12f, energy_units_dram=%.12f, time_units=%.12f\n",
              ctx->cpu_model, ctx->power_units, ctx->energy_units, ctx->energy_units_dram, ctx->time_units);
}

// Replace the requested msrval bits with data the data in situ; first and last are inclusive
static uint64_t replace_bits(uint64_t msrval, uint64_t data, uint8_t first, uint8_t last) {
  assert(first <= last);
  assert(last < 64);
  const uint64_t mask = (((uint64_t) 1 << (last - first + 1)) - 1) << first;
  return (msrval & ~mask) | ((data << first) & mask);
}

int msr_is_zone_enabled(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                        int* en_long, int* en_short) {
  assert(ctx != NULL);
  int ret = 0;
  if (en_long != NULL) {
    *en_long = ((msrval >> EN1_SHIFT) & EN_MASK) == 0x1;
    raplcap_log(DEBUG, "msr_is_zone_enabled: zone=%d, long_term: enabled=%d\n", zone, *en_long);
    ret++;
  }
  if (en_short != NULL && HAS_SHORT_TERM(ctx, zone)) {
    *en_short = ((msrval >> EN2_SHIFT) & EN_MASK) == 0x1;
    raplcap_log(DEBUG, "msr_is_zone_enabled: zone=%d, short_term: enabled=%d\n", zone, *en_short);
    ret++;
  }
  return ret;
}

uint64_t msr_set_zone_enabled(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                              const int* en_long, const int* en_short) {
  assert(ctx != NULL);
  if (en_long != NULL) {
    raplcap_log(DEBUG, "msr_set_zone_enabled: zone=%d, long_term: enabled=%d\n", zone, *en_long);
    msrval = replace_bits(msrval, *en_long ? 0x1 : 0x0, 15, 15);
  }
  if (en_short != NULL && HAS_SHORT_TERM(ctx, zone)) {
    raplcap_log(DEBUG, "msr_set_zone_enabled: zone=%d, short_term: enabled=%d\n", zone, *en_short);
    msrval = replace_bits(msrval, *en_short ? 0x1 : 0x0, 47, 47);
  }
  return msrval;
}

int msr_is_zone_clamped(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                        int* cl_long, int* cl_short) {
  assert(ctx != NULL);
  int ret = 0;
  if (cl_long != NULL) {
    *cl_long = ((msrval >> CL1_SHIFT) & CL_MASK) == 0x1;
    raplcap_log(DEBUG, "msr_is_zone_clamped: zone=%d, long_term: clamp=%d\n", zone, *cl_long);
    ret++;
  }
  if (cl_short != NULL && HAS_SHORT_TERM(ctx, zone)) {
    *cl_short = ((msrval >> CL2_SHIFT) & CL_MASK) == 0x1;
    raplcap_log(DEBUG, "msr_is_zone_clamped: zone=%d, short_term: clamp=%d\n", zone, *cl_short);
    ret++;
  }
  return ret;
}

uint64_t msr_set_zone_clamped(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                              const int* cl_long, const int* cl_short) {
  assert(ctx != NULL);
  if (cl_long != NULL) {
    raplcap_log(DEBUG, "msr_set_zone_clamped: zone=%d, long_term: clamp=%d\n", zone, *cl_long);
    msrval = replace_bits(msrval, *cl_long ? 0x1 : 0x0, 16, 16);
  }
  if (cl_short != NULL && HAS_SHORT_TERM(ctx, zone)) {
    raplcap_log(DEBUG, "msr_set_zone_clamped: zone=%d, short_term: clamp=%d\n", zone, *cl_short);
    msrval = replace_bits(msrval, *cl_short ? 0x1 : 0x0, 48, 48);
  }
  return msrval;
}

int msr_is_zone_locked(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval) {
  assert(ctx != NULL);
  const int ret = (msrval >> (HAS_SHORT_TERM(ctx, zone) ? 63 : 31) & LCK_MASK) == 0x1;
  raplcap_log(DEBUG, "msr_is_zone_locked: zone=%d, locked=%d\n", zone, ret);
  return ret;
}

uint64_t msr_set_zone_locked(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval, int locked) {
  assert(ctx != NULL);
  const uint8_t b = HAS_SHORT_TERM(ctx, zone) ? 63 : 31;
  raplcap_log(DEBUG, "msr_set_zone_locked: zone=%d, locked=%d\n", zone, locked);
  msrval = replace_bits(msrval, locked ? 1 : 0, b, b);
  return msrval;
}

void msr_get_limits(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                    raplcap_limit* limit_long, raplcap_limit* limit_short) {
  assert(ctx != NULL);
  if (limit_long != NULL) {
    limit_long->watts = ctx->cfg[zone].from_msr_pl((msrval >> PL1_SHIFT) & PL_MASK, ctx->power_units);
    limit_long->seconds = ctx->cfg[zone].from_msr_tw((msrval >> TL1_SHIFT) & TL_MASK, ctx->time_units);
    raplcap_log(DEBUG, "msr_get_limits: zone=%d, long_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                zone, limit_long->seconds, limit_long->watts);
  }
  if (limit_short != NULL && HAS_SHORT_TERM(ctx, zone)) {
    limit_short->watts = ctx->cfg[zone].from_msr_pl((msrval >> PL2_SHIFT) & PL_MASK, ctx->power_units);
    if (zone == RAPLCAP_ZONE_PSYS) {
      raplcap_log(DEBUG, "msr_get_limits: Documentation does not specify PSys/Platform short term time window\n");
    }
    limit_short->seconds = ctx->cfg[zone].from_msr_tw((msrval >> TL2_SHIFT) & TL_MASK, ctx->time_units);
    raplcap_log(DEBUG, "msr_get_limits: zone=%d, short_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                zone, limit_short->seconds, limit_short->watts);
  }
}

uint64_t msr_set_limits(const raplcap_msr_ctx* ctx, raplcap_zone zone, uint64_t msrval,
                        const raplcap_limit* limit_long, const raplcap_limit* limit_short) {
  assert(ctx != NULL);
  if (limit_long != NULL) {
    raplcap_log(DEBUG, "msr_set_limits: zone=%d, long_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                zone, limit_long->seconds, limit_long->watts);
    if (limit_long->watts > 0) {
      msrval = replace_bits(msrval, ctx->cfg[zone].to_msr_pl(limit_long->watts, ctx->power_units), 0, 14);
    }
    if (limit_long->seconds > 0) {
      msrval = replace_bits(msrval, ctx->cfg[zone].to_msr_tw(limit_long->seconds, ctx->time_units), 17, 23);
    }
  }
  if (limit_short != NULL && HAS_SHORT_TERM(ctx, zone)) {
    raplcap_log(DEBUG, "msr_set_limits: zone=%d, short_term:\n\ttime=%.12f s\n\tpower=%.12f W\n",
                zone, limit_short->seconds, limit_short->watts);
    if (limit_short->watts > 0) {
      msrval = replace_bits(msrval, ctx->cfg[zone].to_msr_pl(limit_short->watts, ctx->power_units), 32, 46);
    }
    if (limit_short->seconds > 0) {
      // 14.9.3: This field may have a hard-coded value in hardware and ignores values written by software.
      if (zone == RAPLCAP_ZONE_PSYS) {
        // Table 2-38: PSYS has power limit #2, but time window #2 is chosen by the processor
        raplcap_log(WARN, "Not allowed to set PSys/Platform short term time window\n");
      } else {
        msrval = replace_bits(msrval, ctx->cfg[zone].to_msr_tw(limit_short->seconds, ctx->time_units), 49, 55);
      }
    }
  }
  return msrval;
}

double msr_get_energy_counter(const raplcap_msr_ctx* ctx, uint64_t msrval, raplcap_zone zone) {
  assert(ctx != NULL);
  const double joules = ((msrval >> EY_SHIFT) & EY_MASK) *
                        (zone == RAPLCAP_ZONE_DRAM ? ctx->energy_units_dram : ctx->energy_units);
  raplcap_log(DEBUG, "msr_get_energy_counter: joules=%.12f\n", joules);
  return joules;
}

double msr_get_energy_counter_max(const raplcap_msr_ctx* ctx, raplcap_zone zone) {
  assert(ctx != NULL);
  // Get actual rollover value (2^32 * units) rather than max value that can be read ((2^32 - 1) * units)
  const double joules = pow2_u64(32) * (zone == RAPLCAP_ZONE_DRAM ? ctx->energy_units_dram : ctx->energy_units);
  raplcap_log(DEBUG, "msr_get_energy_counter_max: joules=%.12f\n", joules);
  return joules;
}

double msr_get_time_units(const raplcap_msr_ctx* ctx, raplcap_zone zone) {
  assert(ctx != NULL);
  // Airmont PACKAGE domain doesn't use normal time units
  const double sec = ctx->cfg[zone].to_msr_tw == to_msr_tw_atom_airmont ? 5.0 : ctx->time_units;
  raplcap_log(DEBUG, "msr_get_time_units: sec=%.12f\n", sec);
  return sec;
}

double msr_get_power_units(const raplcap_msr_ctx* ctx, raplcap_zone zone) {
  assert(ctx != NULL);
  const double watts = ctx->power_units;
  raplcap_log(DEBUG, "msr_get_power_units: watts=%.12f\n", watts);
  return watts;
}

double msr_get_energy_units(const raplcap_msr_ctx* ctx, raplcap_zone zone) {
  assert(ctx != NULL);
  const double joules = zone == RAPLCAP_ZONE_DRAM ? ctx->energy_units_dram : ctx->energy_units;
  raplcap_log(DEBUG, "msr_get_energy_units: joules=%.12f\n", joules);
  return joules;
}

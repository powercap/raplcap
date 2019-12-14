/**
 * Some unit tests.
 */
/* force assertions */
#undef NDEBUG
#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include "../raplcap-msr-common.h"
#include "../raplcap-cpuid.h"

static double abs_dbl(double a) {
  return a >= 0 ? a : -a;
}

static int equal_dbl(double a, double b) {
  return abs_dbl(a - b) < DBL_EPSILON;
}

static void test_translate_default(void) {
  static const double TU = 0.0009765625; // time unit
  static const double PU = 0.125; // power unit
  raplcap_msr_ctx ctx;
  msr_get_context(&ctx, CPUID_MODEL_SANDYBRIDGE, 0x00000000000A0E03);
  // units
  assert(equal_dbl(ctx.time_units, TU));
  assert(equal_dbl(ctx.power_units, PU));
  // constraints
  assert(ctx.cfg[RAPLCAP_ZONE_PACKAGE].constraints == 2);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_UNCORE].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_DRAM].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_PSYS].constraints == 2);
  // functions
  // example long term
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].from_msr_pl(0x00C8, PU), 25.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_pl(25.0, PU), 0x00C8));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].from_msr_tw(0x6E, TU), 28.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_tw(28.0, TU), 0x6E));
  // example short term
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].from_msr_pl(0x0078, PU), 15.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_pl(15.0, PU), 0x0078));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].from_msr_tw(0x21, TU), 0.002441406250));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_tw(0.002441406250, TU), 0x21));
  // too low (rounds to 0)
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_pl(0.0000001, PU), 0x0));
  // too high
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_pl(10000.0, PU), 0x7FFF));
  // too low (rounds to 0)
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_tw(0.0000001, TU), 0x0));
  // too high
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_tw(10000000.0, TU), 0x7F));
  // TODO: More tests would be good
}

static void test_translate_atom(void) {
  static const double TU = 1.0; // time unit
  static const double PU = 0.032; // power unit
  raplcap_msr_ctx ctx;
  msr_get_context(&ctx, CPUID_MODEL_ATOM_SILVERMONT, 0x5);
  // units - power units only, time unit is always 0, meaning 1 second
  // default value is 0101b, meaning 32 mW
  assert(equal_dbl(ctx.power_units, PU));
  // constraints
  assert(ctx.cfg[RAPLCAP_ZONE_PACKAGE].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_UNCORE].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_DRAM].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_PSYS].constraints == 2);
  // functions - only need to test time windows, power limits are the same as default
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].from_msr_tw(0x0, TU), 1.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].from_msr_tw(0x1, TU), 1.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].from_msr_tw(0x2, TU), 2.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_PACKAGE].from_msr_tw(0x7F, TU), 127.0));
  // too low
  assert(ctx.cfg[RAPLCAP_ZONE_PACKAGE].to_msr_tw(0.99, TU) == 0x0);
  // within range
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(1.0, TU) == 0x1);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(1.49, TU) == 0x1);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(1.51, TU) == 0x2);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(2.0, TU) == 0x2);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(127.0, TU) == 0x7F);
  // too high
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(128.0, TU) == 0x7F);
}

static void test_translate_atom_airmont(void) {
  static const double TU = 0; // dummy time unit
  raplcap_msr_ctx ctx;
  msr_get_context(&ctx, CPUID_MODEL_ATOM_AIRMONT, 0x0);
  // constraints
  assert(ctx.cfg[RAPLCAP_ZONE_PACKAGE].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_UNCORE].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_DRAM].constraints == 1);
  assert(ctx.cfg[RAPLCAP_ZONE_PSYS].constraints == 2);
  // functions - only need to test time windows for CORE, everything else is the same as atom
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x0, TU), 1.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x1, TU), 5.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x2, TU), 10.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x3, TU), 15.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x4, TU), 20.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x5, TU), 25.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x6, TU), 30.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x7, TU), 35.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x8, TU), 40.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0x9, TU), 45.0));
  assert(equal_dbl(ctx.cfg[RAPLCAP_ZONE_CORE].from_msr_tw(0xA, TU), 50.0));
  // too low
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(0.99, TU) == 0x0);
  // within range
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(1.0, TU) == 0x0);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(2.49, TU) == 0x0);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(2.51, TU) == 0x1);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(5, TU) == 0x1);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(10, TU) == 0x2);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(15, TU) == 0x3);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(20, TU) == 0x4);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(25, TU) == 0x5);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(30, TU) == 0x6);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(35, TU) == 0x7);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(40, TU) == 0x8);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(45, TU) == 0x9);
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(50, TU) == 0xA);
  // too high
  assert(ctx.cfg[RAPLCAP_ZONE_CORE].to_msr_tw(50.01, TU) == 0xA);
}

#define TEST_CPU_MODEL CPUID_MODEL_BROADWELL_CORE
#define TEST_UNITS_MSRVAL 0x00000000000A0E03

static const unsigned int TEST_ZONE_COUNT = 2;
static const raplcap_zone TEST_ZONES[] = { RAPLCAP_ZONE_PACKAGE, RAPLCAP_ZONE_CORE };
static const int TEST_ZONES_HAS_SHORT[] = { 1, 0 };

#define MSRVAL_LOCKED_LONG 0x80000000
#define MSRVAL_LOCKED_SHORT 0x8000000000000000
static void test_locked(void) {
  raplcap_msr_ctx ctx;
  uint64_t msrval;
  unsigned int i;
  msr_get_context(&ctx, TEST_CPU_MODEL, TEST_UNITS_MSRVAL);

  for (i = 0; i < TEST_ZONE_COUNT; i++) {
    assert(msr_is_zone_locked(&ctx, TEST_ZONES[i], 0) == 0);
    msrval = msr_set_zone_locked(&ctx, TEST_ZONES[i], 0, 1);
    if (TEST_ZONES_HAS_SHORT[i]) {
      assert(msrval == MSRVAL_LOCKED_SHORT);
    } else {
      assert(msrval == MSRVAL_LOCKED_LONG);
    }
    assert(msr_is_zone_locked(&ctx, TEST_ZONES[i], msrval));
    msrval = msr_set_zone_locked(&ctx, TEST_ZONES[i], msrval, 0);
    assert(msrval == 0);
  }
}

#define MSRVAL_ENABLED_LONG 0x8000
#define MSRVAL_ENABLED_SHORT 0x800000000000
#define MSRVAL_ENABLED_BOTH (MSRVAL_ENABLED_LONG | MSRVAL_ENABLED_SHORT)
static void test_enabled(void) {
  raplcap_msr_ctx ctx;
  uint64_t msrval;
  unsigned int i;
  int en_long;
  int en_short;
  int rc;
  msr_get_context(&ctx, TEST_CPU_MODEL, TEST_UNITS_MSRVAL);

  for (i = 0; i < TEST_ZONE_COUNT; i++) {
    en_long = 1;
    en_short = 1;
    rc = msr_is_zone_enabled(&ctx, TEST_ZONES[i], 0, &en_long, &en_short);
    assert(en_long == 0);
    if (TEST_ZONES_HAS_SHORT[i]) {
      assert(rc == 2);
      assert(en_short == 0);
    } else {
      assert(rc == 1);
      assert(en_short);
    }
    en_long = 1;
    msrval = msr_set_zone_enabled(&ctx, TEST_ZONES[i], 0, &en_long, NULL);
    assert(msrval == MSRVAL_ENABLED_LONG);
    en_long = 0;
    en_short = 0;
    msr_is_zone_enabled(&ctx, TEST_ZONES[i], msrval, &en_long, &en_short);
    assert(en_long);
    assert(en_short == 0);
    en_short = 1;
    msrval = msr_set_zone_enabled(&ctx, TEST_ZONES[i], 0, 0, &en_short);
    en_long = 0;
    en_short = 0;
    msr_is_zone_enabled(&ctx, TEST_ZONES[i], msrval, &en_long, &en_short);
    assert(!en_long);
    if (TEST_ZONES_HAS_SHORT[i]) {
      assert(msrval == MSRVAL_ENABLED_SHORT);
      assert(en_short);
    } else {
      assert(msrval == 0);
      assert(en_short == 0);
    }
    en_long = 1;
    en_short = 1;
    msrval = msr_set_zone_enabled(&ctx, TEST_ZONES[i], 0, &en_long, &en_short);
    en_long = 0;
    en_short = 0;
    msr_is_zone_enabled(&ctx, TEST_ZONES[i], msrval, &en_long, &en_short);
    assert(en_long);
    if (TEST_ZONES_HAS_SHORT[i]) {
      assert(msrval == MSRVAL_ENABLED_BOTH);
      assert(en_short);
    } else {
      assert(msrval == MSRVAL_ENABLED_LONG);
      assert(en_short == 0);
    }
  }
}

#define MSRVAL_CLAMPING_LONG 0x10000
#define MSRVAL_CLAMPING_SHORT 0x1000000000000
#define MSRVAL_CLAMPING_BOTH (MSRVAL_CLAMPING_LONG | MSRVAL_CLAMPING_SHORT)
static void test_clamping(void) {
  raplcap_msr_ctx ctx;
  uint64_t msrval;
  unsigned int i;
  int cl_long;
  int cl_short;
  int rc;
  msr_get_context(&ctx, TEST_CPU_MODEL, TEST_UNITS_MSRVAL);

  for (i = 0; i < TEST_ZONE_COUNT; i++) {
    cl_long = 1;
    cl_short = 1;
    rc = msr_is_zone_clamped(&ctx, TEST_ZONES[i], 0, &cl_long, &cl_short);
    assert(cl_long == 0);
    if (TEST_ZONES_HAS_SHORT[i]) {
      assert(rc == 2);
      assert(cl_short == 0);
    } else {
      assert(rc == 1);
      assert(cl_short);
    }
    cl_long = 1;
    msrval = msr_set_zone_clamped(&ctx, TEST_ZONES[i], 0, &cl_long, NULL);
    assert(msrval == MSRVAL_CLAMPING_LONG);
    cl_long = 0;
    cl_short = 0;
    msr_is_zone_clamped(&ctx, TEST_ZONES[i], msrval, &cl_long, &cl_short);
    assert(cl_long);
    assert(cl_short == 0);
    cl_short = 1;
    msrval = msr_set_zone_clamped(&ctx, TEST_ZONES[i], 0, 0, &cl_short);
    cl_long = 0;
    cl_short = 0;
    msr_is_zone_clamped(&ctx, TEST_ZONES[i], msrval, &cl_long, &cl_short);
    assert(!cl_long);
    if (TEST_ZONES_HAS_SHORT[i]) {
      assert(msrval == MSRVAL_CLAMPING_SHORT);
      assert(cl_short);
    } else {
      assert(msrval == 0);
      assert(cl_short == 0);
    }
    cl_long = 1;
    cl_short = 1;
    msrval = msr_set_zone_clamped(&ctx, TEST_ZONES[i], 0, &cl_long, &cl_short);
    cl_long = 0;
    cl_short = 0;
    msr_is_zone_clamped(&ctx, TEST_ZONES[i], msrval, &cl_long, &cl_short);
    assert(cl_long);
    if (TEST_ZONES_HAS_SHORT[i]) {
      assert(msrval == MSRVAL_CLAMPING_BOTH);
      assert(cl_short);
    } else {
      assert(msrval == MSRVAL_CLAMPING_LONG);
      assert(cl_short == 0);
    }
  }
}

int main(void) {
  // test the private translate functions
  test_translate_default();
  test_translate_atom();
  test_translate_atom_airmont();
  // test boolean bit fields
  test_locked();
  test_enabled();
  test_clamping();
  // TODO: Test additional functions (power/time/energy units...)
  return 0;
}

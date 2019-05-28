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

// TODO: Test additional functions (cpu_model, enabled, clamping, energy...)

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

int main(void) {
  // test the private translate functions
  test_translate_default();
  test_translate_atom();
  test_translate_atom_airmont();
  // TODO: Test functions in the header
  return 0;
}

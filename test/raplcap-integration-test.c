/**
 * Requires a functioning RAPL implementation with appropriate privileges to run.
 */
/* force assertions */
#undef NDEBUG
#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "raplcap.h"

#define NZONES (RAPLCAP_ZONE_PSYS + 1)

static const char* ZONE_NAMES[NZONES] = {
  "PACKAGE",
  "CORE",
  "UNCORE",
  "DRAM",
  "PSYS"
};

static double abs_dbl(double a) {
  return a >= 0 ? a : -a;
}

static int equal_dbl(double a, double b) {
  return abs_dbl(a - b) < DBL_EPSILON;
}

static void test_set(const raplcap_limit* ll, const raplcap_limit* ls, raplcap* rc, uint32_t p, uint32_t i) {
  // TODO: What about enabled status? Do we need to set/reset this?
  raplcap_limit ll_new, ls_new, ll_verify, ls_verify;
  memcpy(&ll_new, ll, sizeof(raplcap_limit));
  memcpy(&ls_new, ls, sizeof(raplcap_limit));
  // increase power by 1 W and double the time interval
  ll_new.watts += 1.0;
  ll_new.seconds *= 2.0;
  ls_new.watts += 1.0;
  ls_new.seconds *= 2.0;
  printf("    Testing raplcap_set_limits(...)\n");
  printf("    Setting (new): ll_w=%f, ll_s=%f, ls_w=%f, ls_s=%f\n",
         ll_new.watts, ll_new.seconds, ls_new.watts, ls_new.seconds);
  assert(raplcap_set_limits(rc, p, (raplcap_zone) i, &ll_new, &ls_new) == 0);
  // verify set
  assert(raplcap_get_limits(rc, p, (raplcap_zone) i, &ll_verify, &ls_verify) == 0);
  equal_dbl(ll_new.watts, ll_verify.watts);
  equal_dbl(ll_new.seconds, ll_verify.seconds);
  equal_dbl(ls_new.watts, ls_verify.watts);
  equal_dbl(ls_new.seconds, ls_verify.seconds);
  // reset to original values and verify
  printf("    Setting (old): ll_w=%f, ll_s=%f, ls_w=%f, ls_s=%f\n", ll->watts, ll->seconds, ls->watts, ls->seconds);
  assert(raplcap_set_limits(rc, p, (raplcap_zone) i, ll, ls) == 0);
  assert(raplcap_get_limits(rc, p, (raplcap_zone) i, &ll_verify, &ls_verify) == 0);
  equal_dbl(ll->watts, ll_verify.watts);
  equal_dbl(ll->seconds, ll_verify.seconds);
  equal_dbl(ls->watts, ls_verify.watts);
  equal_dbl(ls->seconds, ls_verify.seconds);
}

static void test(raplcap* rc, int ro) {
  raplcap_limit ll, ls;
  uint32_t i, p;
  int supported, enabled;
  double joules;
  printf("  Testing raplcap_get_num_packages(NULL)\n");
  uint32_t n_pkg = raplcap_get_num_packages(NULL);
  assert(n_pkg > 0);
  printf("  Testing raplcap_init(...)\n");
  assert(raplcap_init(rc) == 0);
  if (rc != NULL) {
    printf("  Testing raplcap_get_num_packages(rc)\n");
    assert(raplcap_get_num_packages(rc) == n_pkg);
    // verify that init and raplcap_get_num_packages find the same number
    assert(n_pkg == rc->nsockets);
  }
  for (p = 0; p < n_pkg; p++) {
    for (i = 0; i < NZONES; i++) {
      printf("  Package %d, zone %d (%s)...\n", p, i, ZONE_NAMES[i]);
      printf("    Testing raplcap_is_zone_supported(...)\n");
      supported = raplcap_is_zone_supported(rc, p, (raplcap_zone) i);
      assert(supported >= 0);
      if (supported) {
        printf("    Testing raplcap_is_zone_enabled(...)\n");
        enabled = raplcap_is_zone_enabled(rc, p, (raplcap_zone) i);
        assert(enabled >= 0);
        // PACKAGE zone cannot be disabled completely in some impls (e.g., powercap)
        if ((raplcap_zone) i != RAPLCAP_ZONE_PACKAGE) {
          printf("    Testing raplcap_set_zone_enabled(...)\n");
          assert(raplcap_set_zone_enabled(rc, p, (raplcap_zone) i, !enabled) == 0);
          assert(raplcap_is_zone_enabled(rc, p, (raplcap_zone) i) == !enabled);
          assert(raplcap_set_zone_enabled(rc, p, (raplcap_zone) i, enabled) == 0);
          assert(raplcap_is_zone_enabled(rc, p, (raplcap_zone) i) == enabled);
        }
        ll.seconds = -1;
        ll.watts = -1;
        ls.seconds = -1;
        ls.seconds = -1;
        printf("    Testing raplcap_get_limits(...)\n");
        assert(raplcap_get_limits(rc, p, (raplcap_zone) i, &ll, &ls) == 0);
        assert(ll.seconds > 0);
        assert(ll.watts >= 0);
        // can't assert anything about short term for PACKAGE, it's not always present, e.g. on some Atom CPUs
        if (i == RAPLCAP_ZONE_PSYS) {
          assert(ls.seconds > 0);
          assert(ls.watts >= 0);
        }
        printf("    Testing raplcap_get_energy_counter(...)\n");
        joules = raplcap_get_energy_counter(rc, p, (raplcap_zone) i);
        assert(joules >= 0);
        printf("    Testing raplcap_get_energy_counter_max(...)\n");
        joules = raplcap_get_energy_counter_max(rc, p, (raplcap_zone) i);
        assert(joules >= 0);
        if (!ro) {
          test_set(&ll, &ls, rc, p, i);
        }
      } else{
        printf("    Zone not supported, continuing...\n");
      }
    }
  }
  // test bad zone values
  printf("  Testing bad zone values\n");
  assert(raplcap_is_zone_supported(rc, 0, (raplcap_zone) NZONES) < 0);
  assert(raplcap_is_zone_supported(rc, 0, (raplcap_zone) -1) < 0);
  // test bad package values
  printf("  Testing bad package value\n");
  assert(raplcap_is_zone_supported(rc, p, RAPLCAP_ZONE_PACKAGE) < 0);
  printf("  Testing raplcap_destroy(...)\n");
  assert(raplcap_destroy(rc) == 0);
}

int main(int argc, char** argv) {
  int ro = 0;
  raplcap rc;
  printf("Usage: %s [read_only_flag]\n", argv[0]);
  if (argc > 1) {
    ro = atoi(argv[1]);
  }
  printf("Testing global context...\n");
  test(NULL, ro);
  printf("\nTesting local context...\n");
  test(&rc, ro);
  printf("\nTests successful\n");
  return 0;
}

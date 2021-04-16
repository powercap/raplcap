/**
 * Get/set RAPL values.
 *
 * @author Connor Imes
 * @date 2016-05-13
 */
// for setenv
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "raplcap.h"
#include "raplcap-common.h"
#ifdef RAPLCAP_msr
#include "raplcap-msr.h"
#endif // RAPLCAP_msr

typedef struct rapl_configure_ctx {
  int get_packages;
  int get_die;
  raplcap_zone zone;
  raplcap_constraint constraint;
  unsigned int pkg;
  unsigned int die;
  int enabled;
  int set_enabled;
  int set_long;
  raplcap_limit limit_long;
  int set_short;
  raplcap_limit limit_short;
  int set_constraint;
  raplcap_limit limit_constraint;
#ifdef RAPLCAP_msr
  int clamped;
  int set_clamped;
  int set_locked;
#endif // RAPLCAP_msr
} rapl_configure_ctx;

static const char* prog;
static const char short_options[] = "nNc:d:z:l:t:p:e:s:w:S:W:C:Lh";
static const struct option long_options[] = {
  {"npackages",no_argument,       NULL, 'n'},
  {"nsockets", no_argument,       NULL, 'n'}, // deprecated, no longer documented
  {"ndie",     no_argument,       NULL, 'N'},
  {"package",  required_argument, NULL, 'c'},
  {"socket",   required_argument, NULL, 'c'}, // deprecated, no longer documented
  {"die",      required_argument, NULL, 'd'},
  {"zone",     required_argument, NULL, 'z'},
  {"limit",    required_argument, NULL, 'l'},
  {"time",     required_argument, NULL, 't'},
  {"power",    required_argument, NULL, 'p'},
  {"enabled",  required_argument, NULL, 'e'},
  {"seconds0", required_argument, NULL, 's'},
  {"watts0",   required_argument, NULL, 'w'},
  {"seconds1", required_argument, NULL, 'S'},
  {"watts1",   required_argument, NULL, 'W'},
#ifdef RAPLCAP_msr
  {"clamped",  required_argument, NULL, 'C'},
  {"locked",   no_argument,       NULL, 'L'},
#endif // RAPLCAP_msr
  {"help",     no_argument,       NULL, 'h'},
  {0, 0, 0, 0}
};

static void print_usage(int exit_code) {
  fprintf(exit_code ? stderr : stdout,
          "Usage: %s [OPTION]...\n"
          "Options:\n"
          "  -h, --help               Print this message and exit\n"
          "  -n, --npackages          Print the number of packages found and exit\n"
          "  -N, --ndie               Print the number of die found for a package and exit\n"
          "  -c, --package=PACKAGE    The processor package (0 by default)\n"
          "  -d, --die=DIE            The package die (0 by default)\n"
          "  -z, --zone=ZONE          Which zone/domain use. Allowable values:\n"
          "                           PACKAGE - a processor package (default)\n"
          "                           CORE - core power plane\n"
          "                           UNCORE - uncore power plane (client systems only)\n"
          "                           DRAM - main memory (server systems only)\n"
          "                           PSYS - the entire platform (Skylake and newer only)\n"
          "  -l, --limit=CONSTRAINT   Which limit/constraint to use. Allowable values:\n"
          "                           LONG - long term (default)\n"
          "                           SHORT - short term (PACKAGE & PSYS only)\n"
          "                           PEAK - peak power (Tiger Lake PACKAGE only)\n"
          "  -t, --time=SECONDS       Constraint's time window\n"
          "  -p, --power=WATTS        Constraint's power limit\n"
          "  -e, --enabled=1|0        Enable/disable a zone\n"
#ifdef RAPLCAP_msr
          "  -C, --clamped=1|0        Clamp/unclamp a zone\n"
          "                           Clamping is automatically set when enabling\n"
          "                           Therefore, you MUST explicitly set --clamped=0 when\n"
          "                           setting limits (since zones are auto-enabled)\n"
          "  -L, --locked             Lock a zone (a core RESET is required to unlock)\n"
#endif // RAPLCAP_msr
          "The following allow setting long and short term constraints simultaneously:\n"
          "  -s, --seconds0=SECONDS   Long term time window\n"
          "  -w, --watts0=WATTS       Long term power limit\n"
          "  -S, --seconds1=SECONDS   Short term time window (PACKAGE & PSYS only)\n"
          "  -W, --watts1=WATTS       Short term power limit (PACKAGE & PSYS only)\n"
          "\nCurrent values are printed if no flags, or only package, die, and/or zone flags are specified.\n"
          "Otherwise, specified values are set while other values remain unmodified.\n"
          "\nDeprecated behaviors that will change in the future:\n"
          "- When setting values, zones are automatically enabled unless -e/--enabled is explicitly set to 0. "
          "Automatic enabling will be discontinued in the future - "
          "set -e/--enabled explicitly if you are unsure of the prior enabled status.\n"
          "- When getting values for zones with a single constraint, \"watts\" and \"seconds\" are used as value keys. "
          "Fully-qualified names will be used in the future to indicate the actual constraint, "
          "e.g., \"watts_long\" and \"seconds_long\".\n",
          prog);
  exit(exit_code);
}

// something reasonably outside of errno range
#define PRINT_LIMIT_IGNORE -1000

static void print_limits(int enabled, int locked, int clamped,
                         double watts_long, double seconds_long,
                         double watts_short, double seconds_short,
                         double locked_peak, double watts_peak,
                         double joules, double joules_max) {
  // Note: simply using %f (6 decimal places) doesn't provide sufficient precision
  const char* en = enabled < 0 ? "unknown" : (enabled ? "true" : "false");
  const char* lck = locked < 0 ? "unknown" : (locked ? "true" : "false");
  const char* clmp = clamped < 0 ? "unknown" : (clamped ? "true" : "false");
  // time window can never be 0, so if it's > 0, the short term constraint exists
  printf("%13s: %s\n", "enabled", en);
  if (clamped != PRINT_LIMIT_IGNORE) {
    printf("%13s: %s\n", "clamped", clmp);
  }
  if (locked != PRINT_LIMIT_IGNORE) {
    printf("%13s: %s\n", "locked", lck);
  }
  if (seconds_short > 0) {
    printf("%13s: %.12f\n", "watts_long", watts_long);
    printf("%13s: %.12f\n", "seconds_long", seconds_long);
    printf("%13s: %.12f\n", "watts_short", watts_short);
    printf("%13s: %.12f\n", "seconds_short", seconds_short);
    if (locked_peak != PRINT_LIMIT_IGNORE) {
      printf("%13s: %s\n", "locked_peak", (locked_peak ? "true" : "false"));
    }
    if (watts_peak > 0) {
      printf("%13s: %.12f\n", "watts_peak", watts_peak);
    }
  } else {
    printf("%13s: %.12f\n", "watts", watts_long);
    printf("%13s: %.12f\n", "seconds", seconds_long);
  }
  if (joules >= 0) {
    printf("%13s: %.12f\n", "joules", joules);
  }
  if (joules_max >= 0) {
    printf("%13s: %.12f\n", "joules_max", joules_max);
  }
}

static void print_error_continue(const char* msg) {
  perror(msg);
  fprintf(stderr, "Trying to proceed anyway...\n");
}

static int configure_limits(const rapl_configure_ctx* c) {
  assert(c != NULL);
  int ret;
  // set limits
  if ((c->set_long || c->set_short)) {
    if ((ret = raplcap_pd_set_limits(NULL, c->pkg, c->die, c->zone,
                                     c->set_long ? &c->limit_long : NULL,
                                     c->set_short ? &c->limit_short : NULL))) {
      perror("Failed to set limits");
      return ret;
    }
  }
  if (c->set_constraint) {
    if ((ret = raplcap_pd_set_limit(NULL, c->pkg, c->die, c->zone, c->constraint, &c->limit_constraint))) {
      perror("Failed to set limit");
      return ret;
    }
  }

  // enable/disable if requested, otherwise automatically enable
  // No enabled field for peak power
  if (c->set_long || c->set_short ||
      c->constraint == RAPLCAP_CONSTRAINT_LONG_TERM || c->constraint == RAPLCAP_CONSTRAINT_SHORT_TERM) {
    if ((ret = raplcap_pd_set_zone_enabled(NULL, c->pkg, c->die, c->zone, (c->set_enabled ? c->enabled : 1)))) {
      perror("Failed to enable/disable zone");
      return ret;
    }
  }

#ifdef RAPLCAP_msr
  // Note: Enabling automatically sets clamping AND we auto-enable when configuring unless explicitly requested not to.
  //       As a result:
  //       1) We set clamping here AFTER enabling in case clamping was requested to be off
  //       2) The user must always explicitly request clamping to be off when setting RAPL limits
  // No clamped field for peak power
  if (c->set_clamped) {
    if (c->set_long || c->set_short ||
        c->constraint == RAPLCAP_CONSTRAINT_LONG_TERM || c->constraint == RAPLCAP_CONSTRAINT_SHORT_TERM) {
      if ((ret = raplcap_msr_pd_set_zone_clamped(NULL, c->pkg, c->die, c->zone, c->clamped))) {
        perror("Failed to clamp/unclamp zone");
        return ret;
      }
    }
  }

  if (c->set_locked) {
    if (c->set_long || c->set_short ||
        c->constraint == RAPLCAP_CONSTRAINT_LONG_TERM || c->constraint == RAPLCAP_CONSTRAINT_SHORT_TERM) {
      if ((ret = raplcap_msr_pd_set_zone_locked(NULL, c->pkg, c->die, c->zone))) {
        perror("Failed to lock zone");
        return ret;
      }
    }
    // Intentionally not an "else if" - user can set long/short term using -s/-w/-S/-W and also use -l for peak power
    // This if statement isn't strictly necessary, but prevents setting locked twice if constraint is long or short term
    if (c->constraint == RAPLCAP_CONSTRAINT_PEAK_POWER) {
      if ((ret = raplcap_msr_pd_set_locked(NULL, c->pkg, c->die, c->zone, c->constraint))) {
        perror("Failed to lock peak power");
        return ret;
      }
    }
  }
#endif // RAPLCAP_msr
  return 0;
}

static int get_limits(unsigned int pkg, unsigned int die, raplcap_zone zone) {
  raplcap_limit ll = { 0 };
  raplcap_limit ls = { 0 };
  raplcap_limit lp = { 0 };
  double joules;
  double joules_max;
  int locked = PRINT_LIMIT_IGNORE;
  int clamped = PRINT_LIMIT_IGNORE;
  int locked_peak = PRINT_LIMIT_IGNORE;
  int ret;
  int enabled = raplcap_pd_is_zone_enabled(NULL, pkg, die, zone);
  if (enabled < 0) {
    print_error_continue("Failed to determine if zone is enabled");
  }
#ifdef RAPLCAP_msr
  locked = raplcap_msr_pd_is_zone_locked(NULL, pkg, die, zone);
  if (locked < 0) {
    print_error_continue("Failed to determine if zone is locked");
  }
  clamped = raplcap_msr_pd_is_zone_clamped(NULL, pkg, die, zone);
  if (clamped < 0) {
    print_error_continue("Failed to determine if zone is clamped");
  }
#endif // RAPLCAP_msr
  if ((ret = raplcap_pd_get_limits(NULL, pkg, die, zone, &ll, &ls))) {
    perror("Failed to get limits");
    return ret;
  }
  if (raplcap_pd_is_constraint_supported(NULL, pkg, die, zone, RAPLCAP_CONSTRAINT_PEAK_POWER)) {
#ifdef RAPLCAP_msr
    locked_peak = raplcap_msr_pd_is_locked(NULL, pkg, die, zone, RAPLCAP_CONSTRAINT_PEAK_POWER);
    if (locked < 0) {
      print_error_continue("Failed to determine if peak power is locked");
    }
#endif // RAPLCAP_msr
    if ((ret = raplcap_pd_get_limit(NULL, pkg, die, zone, RAPLCAP_CONSTRAINT_PEAK_POWER, &lp))) {
      perror("Failed to get peak power limit");
      return ret;
    }
  }
  // we'll consider energy counter information to be optional
  joules = raplcap_pd_get_energy_counter(NULL, pkg, die, zone);
  joules_max = raplcap_pd_get_energy_counter_max(NULL, pkg, die, zone);
  print_limits(enabled, locked, clamped,
               ll.watts, ll.seconds, ls.watts, ls.seconds,
               locked_peak, lp.watts,
               joules, joules_max);
  return ret;
}

#define SET_VAL(optarg, val, set_val) \
  if ((val = atof(optarg)) <= 0) { \
    fprintf(stderr, "Time window and power limit values must be > 0\n"); \
    print_usage(1); \
  } \
  set_val = 1

int main(int argc, char** argv) {
  rapl_configure_ctx ctx = { 0 };
  int ret = 0;
  int c;
  int supported;
  uint32_t count;
  prog = argv[0];
  int is_read_only;

  // parse parameters
  while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
    switch (c) {
      case 'h':
        print_usage(0);
        break;
      case 'c':
        ctx.pkg = atoi(optarg);
        break;
      case 'd':
        ctx.die = atoi(optarg);
        break;
      case 'n':
        ctx.get_packages = 1;
        break;
      case 'N':
        ctx.get_die = 1;
        break;
      case 'z':
        if (!strcmp(optarg, "PACKAGE")) {
          ctx.zone = RAPLCAP_ZONE_PACKAGE;
        } else if (!strcmp(optarg, "CORE")) {
          ctx.zone = RAPLCAP_ZONE_CORE;
        } else if (!strcmp(optarg, "UNCORE")) {
          ctx.zone = RAPLCAP_ZONE_UNCORE;
        } else if (!strcmp(optarg, "DRAM")) {
          ctx.zone = RAPLCAP_ZONE_DRAM;
        } else if (!strcmp(optarg, "PSYS")) {
          ctx.zone = RAPLCAP_ZONE_PSYS;
        } else {
          print_usage(1);
        }
        break;
      case 'l':
        if (!strcmp(optarg, "LONG")) {
          ctx.constraint = RAPLCAP_CONSTRAINT_LONG_TERM;
        } else if (!strcmp(optarg, "SHORT")) {
          ctx.constraint = RAPLCAP_CONSTRAINT_SHORT_TERM;
        } else if (!strcmp(optarg, "PEAK")) {
          ctx.constraint = RAPLCAP_CONSTRAINT_PEAK_POWER;
        } else {
          print_usage(1);
        }
        break;
      case 't':
        SET_VAL(optarg, ctx.limit_constraint.seconds, ctx.set_constraint);
        break;
      case 'p':
        SET_VAL(optarg, ctx.limit_constraint.watts, ctx.set_constraint);
        break;
      case 'e':
        ctx.enabled = atoi(optarg);
        ctx.set_enabled = 1;
        break;
      case 's':
        SET_VAL(optarg, ctx.limit_long.seconds, ctx.set_long);
        break;
      case 'w':
        SET_VAL(optarg, ctx.limit_long.watts, ctx.set_long);
        break;
      case 'S':
        SET_VAL(optarg, ctx.limit_short.seconds, ctx.set_short);
        break;
      case 'W':
        SET_VAL(optarg, ctx.limit_short.watts, ctx.set_short);
        break;
#ifdef RAPLCAP_msr
      case 'C':
        ctx.clamped = atoi(optarg);
        ctx.set_clamped = 1;
        break;
      case 'L':
        ctx.set_locked = 1;
        break;
#endif // RAPLCAP_msr
      case '?':
      default:
        print_usage(1);
        break;
    }
  }

  // just print the number of packages or die and exit
  // these are often unprivileged operations since we don't need to initialize a raplcap instance
  if (ctx.get_packages) {
    count = raplcap_get_num_packages(NULL);
    if (count == 0) {
      perror("Failed to get number of packages");
      return 1;
    }
    printf("%"PRIu32"\n", count);
    return 0;
  }
  if (ctx.get_die) {
    count = raplcap_get_num_die(NULL, ctx.pkg);
    if (count == 0) {
      perror("Failed to get number of die");
      return 1;
    }
    printf("%"PRIu32"\n", count);
    return 0;
  }

  // initialize
  is_read_only = !ctx.set_enabled && !ctx.set_long && !ctx.set_short && !ctx.set_constraint;
#ifdef RAPLCAP_msr
  is_read_only &= !ctx.set_clamped && !ctx.set_locked;
#endif // RAPLCAP_msr
#ifndef _WIN32
  if (is_read_only) {
    // request read-only access (not supported by all implementations, therefore not guaranteed)
    setenv(ENV_RAPLCAP_READ_ONLY, "1", 0);
  }
#endif
  if (raplcap_init(NULL)) {
    perror("Failed to initialize");
    return 1;
  }

  supported = raplcap_pd_is_zone_supported(NULL, ctx.pkg, ctx.die, ctx.zone);
  if (supported == 0) {
    fprintf(stderr, "Zone not supported\n");
    ret = -1;
  } else {
    if (supported < 0) {
      print_error_continue("Failed to determine if zone is supported");
    }
    // perform requested action
    if (is_read_only) {
      ret = get_limits(ctx.pkg, ctx.die, ctx.zone);
    } else {
      // only checks the -l constraint, not short term constraints set with -W and/or -S, which may not be supported
      supported = raplcap_pd_is_constraint_supported(NULL, ctx.pkg, ctx.die, ctx.zone, ctx.constraint);
      if (supported == 0) {
        fprintf(stderr, "Constraint not supported\n");
        ret = -1;
      } else {
        if (supported < 0) {
          print_error_continue("Failed to determine if constraint is supported");
        }
        if (ctx.set_constraint && ctx.constraint == RAPLCAP_CONSTRAINT_PEAK_POWER && ctx.limit_constraint.seconds != 0) {
          fprintf(stderr, "Cannot set a time window for peak power\n");
          ret = -1;
        } else {
          if (ctx.set_short) {
            supported = raplcap_pd_is_constraint_supported(NULL, ctx.pkg, ctx.die, ctx.zone, RAPLCAP_CONSTRAINT_SHORT_TERM);
            if (supported < 0) {
              print_error_continue("Failed to determine if short term constraint is supported");
            } else if (supported == 0) {
              fprintf(stderr, "Short term constraint not supported for requested zone. Values will be ignored.\n");
              fprintf(stderr, "Warning: This behavior is deprecated. In the future, setting -S/--seconds1 or "
                              "-W/--watts1 for zones without short term constraints will be considered an error.\n");
              // TODO: Fail with an error, which is consistent with -l constraint checking behavior
              //       This check should then be moved above to where we check the -l constraint
            }
          }
          ret = configure_limits(&ctx);
        }
      }
    }
  }

  // cleanup
  if (raplcap_destroy(NULL)) {
    perror("Failed to clean up");
  }

  return ret;
}

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
  int get_sockets;
  int get_die;
  raplcap_zone zone;
  unsigned int socket;
  int enabled;
  int set_enabled;
  int set_long;
  double watts_long;
  double sec_long;
  int set_short;
  double watts_short;
  double sec_short;
#ifdef RAPLCAP_msr
  int clamped;
  int set_clamped;
  int set_locked;
#endif // RAPLCAP_msr
} rapl_configure_ctx;

static const char* prog;
static const char short_options[] = "nNc:z:e:s:w:S:W:C:Lh";
static const struct option long_options[] = {
  {"nsockets", no_argument,       NULL, 'n'},
  {"ndie",     no_argument,       NULL, 'N'},
  {"socket",   required_argument, NULL, 'c'},
  {"zone",     required_argument, NULL, 'z'},
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
          "  -n, --nsockets           Print the number of sockets found and exit\n"
          "  -N, --ndie               Print the number of die found for a socket and exit\n"
          "  -c, --socket=SOCKET      The processor socket (0 by default)\n"
          "  -z, --zone=ZONE          Which zone/domain use. Allowable values:\n"
          "                           PACKAGE - a processor socket (default)\n"
          "                           CORE - core power plane\n"
          "                           UNCORE - uncore power plane (client systems only)\n"
          "                           DRAM - main memory (server systems only)\n"
          "                           PSYS - the entire platform (Skylake and newer only)\n"
          "  -e, --enabled=1|0        Enable/disable a zone\n"
          "  -s, --seconds0=SECONDS   Long term time window\n"
          "  -w, --watts0=WATTS       Long term power limit\n"
          "  -S, --seconds1=SECONDS   Short term time window (PACKAGE & PSYS only)\n"
          "  -W, --watts1=WATTS       Short term power limit (PACKAGE & PSYS only)\n"
#ifdef RAPLCAP_msr
          "  -C, --clamped=1|0        Clamp/unclamp a zone\n"
          "                           Clamping is automatically set when enabling\n"
          "                           Therefore, you MUST explicitly set --clamped=0 when\n"
          "                           setting limits (since zones are auto-enabled)\n"
          "  -L, --locked             Lock a zone (a core RESET is required to unlock)\n"
#endif // RAPLCAP_msr
          "  -h, --help               Print this message and exit\n\n"
          "Current values are printed if no flags, or only socket and/or zone flags, are specified.\n"
          "Otherwise, specified values are set while other values remain unmodified.\n"
          "When setting values, zones are automatically enabled unless -e/--enabled is explicitly set to 0.\n",
          prog);
  exit(exit_code);
}

// something reasonably outside of errno range
#define PRINT_LIMIT_IGNORE -1000

static void print_limits(int enabled, int locked, int clamped,
                         double watts_long, double seconds_long,
                         double watts_short, double seconds_short,
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
  raplcap_limit limit_long;
  raplcap_limit limit_short;
  raplcap_limit* ll = NULL;
  raplcap_limit* ls = NULL;
  int ret = 0;
  if (c->set_long) {
    limit_long.seconds = c->sec_long;
    limit_long.watts = c->watts_long;
    ll = &limit_long;
  }
  if (c->set_short) {
    limit_short.seconds = c->sec_short;
    limit_short.watts = c->watts_short;
    ls = &limit_short;
  }
  // set limits
  if ((c->set_long || c->set_short) && (ret = raplcap_set_limits(NULL, c->socket, c->zone, ll, ls))) {
    perror("Failed to set limits");
    return ret;
  }
  // enable/disable if requested, otherwise automatically enable
  if ((ret = raplcap_set_zone_enabled(NULL, c->socket, c->zone, (c->set_enabled ? c->enabled : 1)))) {
    perror("Failed to enable/disable zone");
    return ret;
  }
#ifdef RAPLCAP_msr
  // Note: Enabling automatically sets clamping AND we auto-enable when configuring unless explicitly requested not to.
  //       As a result:
  //       1) We set clamping here AFTER enabling in case clamping was requested to be off
  //       2) The user must always explicitly request clamping to be off when setting RAPL limits
  if (c->set_clamped && (ret = raplcap_msr_set_zone_clamped(NULL, c->socket, c->zone, c->clamped))) {
    perror("Failed to clamp/unclamp zone");
    return ret;
  }
  if (c->set_locked && (ret = raplcap_msr_set_zone_locked(NULL, c->socket, c->zone))) {
    perror("Failed to lock zone");
    return ret;
  }
#endif // RAPLCAP_msr
  return 0;
}

static int get_limits(unsigned int socket, raplcap_zone zone) {
  raplcap_limit ll = { 0 };
  raplcap_limit ls = { 0 };
  double joules;
  double joules_max;
  int locked = PRINT_LIMIT_IGNORE;
  int clamped = PRINT_LIMIT_IGNORE;
  int ret;
  int enabled = raplcap_is_zone_enabled(NULL, socket, zone);
  if (enabled < 0) {
    print_error_continue("Failed to determine if zone is enabled");
  }
#ifdef RAPLCAP_msr
  locked = raplcap_msr_is_zone_locked(NULL, socket, zone);
  if (locked < 0) {
    print_error_continue("Failed to determine if zone is locked");
  }
  clamped = raplcap_msr_is_zone_clamped(NULL, socket, zone);
  if (clamped < 0) {
    print_error_continue("Failed to determine if zone is clamped");
  }
#endif // RAPLCAP_msr
  if ((ret = raplcap_get_limits(NULL, socket, zone, &ll, &ls))) {
    perror("Failed to get limits");
    return ret;
  }
  // we'll consider energy counter information to be optional
  joules = raplcap_get_energy_counter(NULL, socket, zone);
  joules_max = raplcap_get_energy_counter_max(NULL, socket, zone);
  print_limits(enabled, locked, clamped,
               ll.watts, ll.seconds, ls.watts, ls.seconds,
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
        ctx.socket = atoi(optarg);
        break;
      case 'n':
        ctx.get_sockets = 1;
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
      case 'e':
        ctx.enabled = atoi(optarg);
        ctx.set_enabled = 1;
        break;
      case 's':
        SET_VAL(optarg, ctx.sec_long, ctx.set_long);
        break;
      case 'w':
        SET_VAL(optarg, ctx.watts_long, ctx.set_long);
        break;
      case 'S':
        SET_VAL(optarg, ctx.sec_short, ctx.set_short);
        break;
      case 'W':
        SET_VAL(optarg, ctx.watts_short, ctx.set_short);
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

  // just print the number of sockets or die and exit
  // these are often unprivileged operations since we don't need to initialize a raplcap instance
  if (ctx.get_sockets) {
    count = raplcap_get_num_sockets(NULL);
    if (count == 0) {
      perror("Failed to get number of sockets");
      return 1;
    }
    printf("%"PRIu32"\n", count);
    return 0;
  }
  if (ctx.get_die) {
    count = raplcap_get_num_die(NULL, ctx.socket);
    if (count == 0) {
      perror("Failed to get number of die");
      return 1;
    }
    printf("%"PRIu32"\n", count);
    return 0;
  }

  // initialize
  is_read_only = !ctx.set_enabled && !ctx.set_long && !ctx.set_short;
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

  supported = raplcap_is_zone_supported(NULL, ctx.socket, ctx.zone);
  if (supported == 0) {
    fprintf(stderr, "Zone not supported\n");
    ret = -1;
  } else {
    if (supported < 0) {
      print_error_continue("Failed to determine if zone is supported");
    }
    // perform requested action
    if (is_read_only) {
      ret = get_limits(ctx.socket, ctx.zone);
    } else {
      ret = configure_limits(&ctx);
    }
  }

  // cleanup
  if (raplcap_destroy(NULL)) {
    perror("Failed to clean up");
  }

  return ret;
}

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

typedef struct rapl_configure_ctx {
  int get_sockets;
  raplcap_zone zone;
  unsigned int socket;
  int set_long;
  double watts_long;
  double sec_long;
  int set_short;
  double watts_short;
  double sec_short;
} rapl_configure_ctx;

static rapl_configure_ctx ctx;
static const char* prog;
static const char short_options[] = "nc:z:s:w:S:W:h";
static const struct option long_options[] = {
  {"nsockets", no_argument,       NULL, 'n'},
  {"socket",   required_argument, NULL, 'c'},
  {"zone",     required_argument, NULL, 'z'},
  {"seconds0", required_argument, NULL, 's'},
  {"watts0",   required_argument, NULL, 'w'},
  {"seconds1", required_argument, NULL, 'S'},
  {"watts1",   required_argument, NULL, 'W'},
  {"help",     no_argument,       NULL, 'h'},
  {0, 0, 0, 0}
};

static void print_usage(int exit_code) {
  fprintf(exit_code ? stderr : stdout,
          "Usage: %s [OPTION]...\n"
          "Options:\n"
          "  -n, --nsockets           Print the number of sockets found and exit\n"
          "  -c, --socket=SOCKET      The processor socket (0 by default)\n"
          "  -z, --zone=ZONE          Which zone/domain use. Allowable values:\n"
          "                           PACKAGE - a processor socket (default)\n"
          "                           CORE - core power plane\n"
          "                           UNCORE - uncore power plane (client systems only)\n"
          "                           DRAM - main memory (server systems only)\n"
          "                           PSYS - the entire platform (Skylake and newer only)\n"
          "  -s, --seconds0=SECONDS   long term time window\n"
          "  -w, --watts0=WATTS       long term power limit\n"
          "  -S, --seconds1=SECONDS   short term time window (PACKAGE & PSYS only)\n"
          "  -W, --watts1=WATTS       short term power limit (PACKAGE & PSYS only)\n"
          "  -h, --help               Print this message and exit\n\n"
          "Unless time or power limits are specified, current values will be printed.\n"
          "If the only values specified are 0, the zone will be disabled.\n\n",
          prog);
  exit(exit_code);
}

static void print_limits(raplcap_zone zone, int enabled,
                         double watts_long, double seconds_long,
                         double watts_short, double seconds_short) {
  // Note: simply using %f (6 decimal places) doesn't provide sufficient precision
  const char* en = enabled < 0 ? "unknown" : (enabled ? "true" : "false");
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_PSYS:
      printf("%13s: %s\n", "enabled", en);
      printf("%13s: %.12f\n", "watts_long", watts_long);
      printf("%13s: %.12f\n", "seconds_long", seconds_long);
      printf("%13s: %.12f\n", "watts_short", watts_short);
      printf("%13s: %.12f\n", "seconds_short", seconds_short);
      break;
    case RAPLCAP_ZONE_CORE:
    case RAPLCAP_ZONE_UNCORE:
    case RAPLCAP_ZONE_DRAM:
    default:
      printf("%7s: %s\n", "enabled", en);
      printf("%7s: %.12f\n", "watts", watts_long);
      printf("%7s: %.12f\n", "seconds", seconds_long);
      break;
  }
}

static void print_enable_error(const char* fn) {
  perror(fn);
  fprintf(stderr, "Trying to proceed anyway...\n");
}

static int configure_limits(const rapl_configure_ctx* c) {
  assert(c != NULL);
  raplcap_limit limit_long;
  raplcap_limit limit_short;
  raplcap_limit* ll = NULL;
  raplcap_limit* ls = NULL;
  int disable = 1;
  if (c->set_long) {
    limit_long.seconds = c->sec_long;
    limit_long.watts = c->watts_long;
    ll = &limit_long;
    disable &= is_zero_dbl(limit_long.seconds) && is_zero_dbl(limit_long.watts);
  }
  if (c->set_short) {
    limit_short.seconds = c->sec_short;
    limit_short.watts = c->watts_short;
    ls = &limit_short;
    disable &= is_zero_dbl(limit_short.seconds) && is_zero_dbl(limit_short.watts);
  }
  if (disable) {
    // all given values were 0 - disable the zone
    return raplcap_set_zone_enabled(NULL, c->socket, c->zone, 0);
  }
  if (raplcap_set_zone_enabled(NULL, c->socket, c->zone, 1)) {
    print_enable_error("raplcap_set_zone_enabled");
  }
  return raplcap_set_limits(NULL, c->socket, c->zone, ll, ls);
}

static int get_limits(unsigned int socket, raplcap_zone zone) {
  raplcap_limit ll;
  raplcap_limit ls;
  memset(&ll, 0, sizeof(raplcap_limit));
  memset(&ls, 0, sizeof(raplcap_limit));
  int enabled = raplcap_is_zone_enabled(NULL, socket, zone);
  if (enabled < 0) {
    print_enable_error("raplcap_is_zone_enabled");
  }
  // short only allowed for package and psys zones
  raplcap_limit* s = (zone == RAPLCAP_ZONE_PACKAGE || zone == RAPLCAP_ZONE_PSYS) ? &ls : NULL;
  if (raplcap_get_limits(NULL, socket, zone, &ll, s)) {
    return -1;
  }
  print_limits(zone, enabled, ll.watts, ll.seconds, ls.watts, ls.seconds);
  return 0;
}

int main(int argc, char** argv) {
  int ret = 0;
  int c;
  int supported;
  uint32_t sockets;
  prog = argv[0];
  int is_read_only;

  // parse parameters
  memset(&ctx, 0, sizeof(rapl_configure_ctx));
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
      case 's':
        ctx.sec_long = atof(optarg);
        ctx.set_long = 1;
        break;
      case 'w':
        ctx.watts_long = atof(optarg);
        ctx.set_long = 1;
        break;
      case 'S':
        ctx.sec_short = atof(optarg);
        ctx.set_short = 1;
        break;
      case 'W':
        ctx.watts_short = atof(optarg);
        ctx.set_short = 1;
        break;
      case '?':
      default:
        print_usage(1);
        break;
    }
  }

  // just print the number of sockets and exit
  // this is often an unprivileged operation since we don't need to initialize a raplcap instance
  if (ctx.get_sockets) {
    sockets = raplcap_get_num_sockets(NULL);
    if (sockets == 0) {
      perror("raplcap_get_num_sockets");
      return 1;
    }
    printf("%"PRIu32"\n", sockets);
    return 0;
  }

  // verify parameters
  if (ctx.watts_short < 0 || ctx.sec_short < 0 || ctx.watts_long < 0 || ctx.sec_long < 0) {
    fprintf(stderr, "Power and interval values must be >= 0\n");
    print_usage(1);
  }

  // initialize
  is_read_only = !ctx.set_long && !ctx.set_short;
#ifndef _WIN32
  if (is_read_only) {
    // request read-only access (not supported by all implementations, therefore not guaranteed)
    setenv(ENV_RAPLCAP_READ_ONLY, "1", 0);
  }
#endif
  if (raplcap_init(NULL)) {
    perror("Init failed");
    return 1;
  }

  supported = raplcap_is_zone_supported(NULL, ctx.socket, ctx.zone);
  if (supported == 0) {
    fprintf(stderr, "Zone not supported\n");
    ret = -1;
  } else {
    if (supported < 0) {
      perror("raplcap_is_zone_supported");
      fprintf(stderr, "Trying to proceed anyway...\n");
    }
    // perform requested action
    if (is_read_only) {
      ret = get_limits(ctx.socket, ctx.zone);
    } else {
      ret = configure_limits(&ctx);
    }
    if (ret) {
      perror("Action failed");
    }
  }

  // cleanup
  if (raplcap_destroy(NULL)) {
    perror("Cleanup failed");
  }

  return ret;
}

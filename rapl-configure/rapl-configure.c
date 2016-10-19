/**
 * Get/set RAPL values.
 *
 * @author Connor Imes
 * @date 2016-05-13
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "raplcap.h"

typedef struct rapl_configure_ctx {
  raplcap_zone zone;
  unsigned int socket;
  int print;
  double watts_long;
  double sec_long;
  double watts_short;
  double sec_short;
} rapl_configure_ctx;

static rapl_configure_ctx ctx;
static const char* prog;
static const char short_options[] = "c:z:ps:w:S:W:h";
static const struct option long_options[] = {
  {"socket",   required_argument, NULL, 'c'},
  {"zone",     required_argument, NULL, 'z'},
  {"seconds0", required_argument, NULL, 's'},
  {"watts0",   required_argument, NULL, 'w'},
  {"seconds1", required_argument, NULL, 'S'},
  {"watts1",   required_argument, NULL, 'W'},
  {"help",     required_argument, NULL, 'h'},
};
static raplcap rc;

void print_usage(int exit_code) {
  fprintf(exit_code ? stderr : stdout,
          "Usage:\n"
          "  %s [options]\n\n"
          "  -c, --socket=SOCKET      The processor socket (0 by default)\n"
          "  -z, --zone=ZONE          Specify what to configure. Allowable values:\n"
          "                           PACKAGE - a processor socket (default)\n"
          "                           CORE - core power plane\n"
          "                           UNCORE - uncore power plane (client systems only)\n"
          "                           DRAM - main memory (server systems only)\n"
          "                           PSYS - the entire SoC (Skylake and newer only)\n"
          "  -s, --seconds0=SECONDS   long_term time window\n"
          "  -w, --watts0=WATTS       long_term power limit\n"
          "  -S, --seconds1=SECONDS   short_term time window (PACKAGE & PSYS only)\n"
          "  -W, --watts1=WATTS       short_term power limit (PACKAGE & PSYS only)\n"
          "  -h, --help               Print this message and exit\n\n"
          "Unless time or power limits are specified, current values will be printed\n\n",
          prog);
  exit(exit_code);
}

static void print_limits(raplcap_zone zone,
                         double watts_long, double seconds_long,
                         double watts_short, double seconds_short) {
  switch (zone) {
    case RAPLCAP_ZONE_PACKAGE:
    case RAPLCAP_ZONE_PSYS:
      printf("%13s: %f\n", "watts_long", watts_long);
      printf("%13s: %f\n", "seconds_long", seconds_long);
      printf("%13s: %f\n", "watts_short", watts_short);
      printf("%13s: %f\n", "seconds_short", seconds_short);
      break;
    case RAPLCAP_ZONE_CORE:
    case RAPLCAP_ZONE_UNCORE:
    case RAPLCAP_ZONE_DRAM:
    default:
      printf("%7s: %f\n", "watts", watts_long);
      printf("%7s: %f\n", "seconds", seconds_long);
      break;
  }
}

int configure_limits(unsigned int socket, raplcap_zone zone,
                     double watts_long, double seconds_long,
                     double watts_short, double seconds_short) {
  assert(seconds_short >= 0);
  assert(watts_short >= 0);
  assert(seconds_long >= 0);
  assert(watts_long >= 0);
  raplcap_limit limit_long;
  raplcap_limit limit_short;
  raplcap_limit* ll = (seconds_long > 0 || watts_long > 0) ? &limit_long : NULL;
  raplcap_limit* ls = (seconds_short > 0 || watts_short > 0) ? &limit_short : NULL;
  limit_long.seconds = seconds_long;
  limit_long.watts = watts_long;
  limit_short.seconds = seconds_short;
  limit_short.watts = watts_short;
  return raplcap_set_limits(socket, &rc, zone, ll, ls);
}

int get_limits(unsigned int socket, raplcap_zone zone) {
  raplcap_limit ll;
  raplcap_limit ls;
  memset(&ll, 0, sizeof(raplcap_limit));
  memset(&ls, 0, sizeof(raplcap_limit));
  if (raplcap_get_limits(socket, &rc, zone, &ll, &ls)) {
    return -1;
  }
  print_limits(zone, ll.watts, ll.seconds, ls.watts, ls.seconds);
  return 0;
}

int main(int argc, char** argv) {
  int ret = 0;
  int c;
  int read = 0;
  prog = argv[0];

  // parse parameters
  while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
    switch (c) {
      case 'h':
        print_usage(0);
        break;
      case 'c':
        ctx.socket = atoi(optarg);
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
        break;
      case 'w':
        ctx.watts_long = atof(optarg);
        break;
      case 'S':
        ctx.sec_short = atof(optarg);
        break;
      case 'W':
        ctx.watts_short = atof(optarg);
        break;
      case '?':
      default:
        print_usage(1);
        break;
    }
  }

  // verify parameters
  if (ctx.watts_short < 0 || ctx.sec_short < 0 || ctx.watts_long < 0 || ctx.sec_long < 0) {
    fprintf(stderr, "Power and interval values must be > 0 (0 values are ignored)\n");
    print_usage(1);
  }
  read = ctx.watts_short == 0 && ctx.sec_short == 0 && ctx.watts_long == 0 && ctx.sec_long == 0;

  // initialize
  if (raplcap_init(&rc)) {
    perror("Init failed");
    return 1;
  }

  // perform requested action
  if (read) {
    ret = get_limits(ctx.socket, ctx.zone);
  } else {
    ret = configure_limits(ctx.socket, ctx.zone, ctx.watts_long, ctx.sec_long, ctx.watts_short, ctx.sec_short);
  }

  if (ret) {
    perror("Action failed");
  }

  // cleanup
  if (raplcap_destroy(&rc)) {
    perror("Cleanup failed");
  }

  return ret;
}

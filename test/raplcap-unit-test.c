/**
 * Tests bad parameters.
 * No way to test good ones without a functioning RAPL implementation, which isn't guaranteed to exist.
 */
/* force assertions */
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "raplcap.h"

int main(void) {
  // basically all we can test is some uninitialized parameters
  // the context can't be complete garbage though, it must be zeroed out - we'll just use the global context
  errno = 0;
  assert(raplcap_is_zone_supported(NULL, 0, RAPLCAP_ZONE_PACKAGE) < 0);
  assert(errno == EINVAL);
  errno = 0;
  assert(raplcap_is_zone_enabled(NULL, 0, RAPLCAP_ZONE_PACKAGE) < 0);
  assert(errno == EINVAL);
  errno = 0;
  assert(raplcap_set_zone_enabled(NULL, 0, RAPLCAP_ZONE_PACKAGE, 0) < 0);
  assert(errno == EINVAL);
  errno = 0;
  assert(raplcap_get_limits(NULL, 0, RAPLCAP_ZONE_PACKAGE, NULL, NULL) < 0);
  assert(errno == EINVAL);
  errno = 0;
  assert(raplcap_set_limits(NULL, 0, RAPLCAP_ZONE_PACKAGE, NULL, NULL) < 0);
  assert(errno == EINVAL);
  // just verify that it doesn't crash (API doesn't specify what to return or whether to set errno in this case)
  raplcap_destroy(NULL);
  // also verifying that it doesn't crash
  raplcap_get_num_sockets(NULL);
  return 0;
}

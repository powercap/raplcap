/**
 * Functions that depend on cpuid info.
 *
 * @author Connor Imes
 * @date 2017-12-13
 */
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "raplcap-common.h"
#include "raplcap-cpuid.h"

typedef struct asm_cpuid_data {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
} asm_cpuid_data;

static void asm_cpuid(uint32_t leaf, asm_cpuid_data* data) {
  assert(data != NULL);
#if defined(__x86_64__) || defined(__i386__)
  __asm__ __volatile__ ("cpuid" :
                        "=a" (data->eax), "=b" (data->ebx), "=c" (data->ecx), "=d" (data->edx) :
                        "a" (leaf));
#else
  #error x86 architecture is required
#endif
}

int cpuid_is_vendor_intel(void) {
  asm_cpuid_data cpudata;
  union {
    char c[16];
    uint32_t i[16 / sizeof(uint32_t)];
  } vendor_id;
  memset(&vendor_id.c, 0, sizeof(vendor_id));
  asm_cpuid(0, &cpudata);
  vendor_id.i[0] = cpudata.ebx;
  vendor_id.i[1] = cpudata.edx;
  vendor_id.i[2] = cpudata.ecx;
  raplcap_log(DEBUG, "cpuid_is_vendor_intel: vendor_id=%s\n", vendor_id.c);
  return !strncmp(vendor_id.c, CPUID_VENDOR_ID_GENUINE_INTEL, sizeof(CPUID_VENDOR_ID_GENUINE_INTEL));
}

void cpuid_get_family_model(uint32_t* family, uint32_t* model) {
  assert(family != NULL);
  assert(model != NULL);
  asm_cpuid_data cpudata;
  asm_cpuid(1, &cpudata);
  // family | extended family (upper 4 bits only) -- must be "6"
  *family = ((cpudata.eax >> 8) & 0xF) | ((cpudata.eax >> 16) & 0xF0);
  // model | processor type (plus two more bits 14:15?)
  *model = ((cpudata.eax >> 4) & 0xF) | ((cpudata.eax >> 12) & 0xF0);
  raplcap_log(DEBUG, "cpuid_get_family_model: cpu_family=%02X, cpu_model=%02X\n", *family, *model);
}

int cpuid_is_cpu_supported(uint32_t family, uint32_t model) {
  if (family == 6) {
    switch (model) {
      case CPUID_MODEL_SANDYBRIDGE:
      case CPUID_MODEL_SANDYBRIDGE_X:
      //
      case CPUID_MODEL_IVYBRIDGE:
      case CPUID_MODEL_IVYBRIDGE_X:
      //
      case CPUID_MODEL_HASWELL:
      case CPUID_MODEL_HASWELL_X:
      case CPUID_MODEL_HASWELL_L:
      case CPUID_MODEL_HASWELL_G:
      //
      case CPUID_MODEL_BROADWELL:
      case CPUID_MODEL_BROADWELL_G:
      case CPUID_MODEL_BROADWELL_X:
      case CPUID_MODEL_BROADWELL_D:
      //
      case CPUID_MODEL_SKYLAKE_L:
      case CPUID_MODEL_SKYLAKE:
      case CPUID_MODEL_SKYLAKE_X:
      //
      case CPUID_MODEL_KABYLAKE_L:
      case CPUID_MODEL_KABYLAKE:
      //
      case CPUID_MODEL_CANNONLAKE_L:
      //
      case CPUID_MODEL_ICELAKE:
      case CPUID_MODEL_ICELAKE_L:
      case CPUID_MODEL_ICELAKE_X:
      case CPUID_MODEL_ICELAKE_D:
      //
      case CPUID_MODEL_COMETLAKE:
      case CPUID_MODEL_COMETLAKE_L:
      //
      case CPUID_MODEL_TIGERLAKE_L:
      case CPUID_MODEL_TIGERLAKE:
      //
      case CPUID_MODEL_ALDERLAKE:
      case CPUID_MODEL_ALDERLAKE_L:
      //
      case CPUID_MODEL_RAPTORLAKE:
      case CPUID_MODEL_RAPTORLAKE_P:
      case CPUID_MODEL_RAPTORLAKE_S:
      //
      case CPUID_MODEL_METEORLAKE_L:
      //
      case CPUID_MODEL_SAPPHIRERAPIDS_X:
      case CPUID_MODEL_EMERALDRAPIDS_X:
      //
      case CPUID_MODEL_XEON_PHI_KNL:
      case CPUID_MODEL_XEON_PHI_KNM:
      //
      case CPUID_MODEL_ATOM_SILVERMONT:
      case CPUID_MODEL_ATOM_SILVERMONT_MID:
      case CPUID_MODEL_ATOM_SILVERMONT_D:
      case CPUID_MODEL_ATOM_AIRMONT:
      case CPUID_MODEL_ATOM_AIRMONT_MID:
      case CPUID_MODEL_ATOM_SOFIA:
      case CPUID_MODEL_ATOM_GOLDMONT:
      case CPUID_MODEL_ATOM_GOLDMONT_D:
      case CPUID_MODEL_ATOM_GOLDMONT_PLUS:
      case CPUID_MODEL_ATOM_TREMONT_D:
      case CPUID_MODEL_ATOM_TREMONT:
      case CPUID_MODEL_ATOM_TREMONT_L:
        return 1;
    }
  }
  return 0;
}

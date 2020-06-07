#ifndef _RAPLCAP_CPUID_H_
#define _RAPLCAP_CPUID_H_

#ifdef __cplusplus
extern "C" {
#endif

#pragma GCC visibility push(hidden)

#define CPUID_VENDOR_ID_GENUINE_INTEL "GenuineIntel"

/* 
 * See: Software Developer's Manual, Volume 4 (May 2019)
 * See: https://en.wikichip.org/wiki/intel/cpuid
 */

//----
// Sandy Bridge is the first to support RAPL
#define CPUID_MODEL_SANDYBRIDGE       0x2A
#define CPUID_MODEL_SANDYBRIDGE_X     0x2D

#define CPUID_MODEL_IVYBRIDGE         0x3A
#define CPUID_MODEL_IVYBRIDGE_X       0x3E

#define CPUID_MODEL_HASWELL           0x3C
#define CPUID_MODEL_HASWELL_X         0x3F
#define CPUID_MODEL_HASWELL_L         0x45
#define CPUID_MODEL_HASWELL_G         0x46

#define CPUID_MODEL_BROADWELL         0x3D
#define CPUID_MODEL_BROADWELL_G       0x47
#define CPUID_MODEL_BROADWELL_X       0x4F
#define CPUID_MODEL_BROADWELL_D       0x56

#define CPUID_MODEL_SKYLAKE_L         0x4E
#define CPUID_MODEL_SKYLAKE_X         0x55
#define CPUID_MODEL_SKYLAKE           0x5E

#define CPUID_MODEL_KABYLAKE_L        0x8E
#define CPUID_MODEL_KABYLAKE          0x9E

#define CPUID_MODEL_CANNONLAKE_L      0x66

#define CPUID_MODEL_ICELAKE           0x7D
#define CPUID_MODEL_ICELAKE_L         0x7E
#define CPUID_MODEL_ICELAKE_X         0x6A
#define CPUID_MODEL_ICELAKE_D         0x6C

#define CPUID_MODEL_COMETLAKE         0xA5
#define CPUID_MODEL_COMETLAKE_L       0xA6

#define CPUID_MODEL_XEON_PHI_KNL      0x57
#define CPUID_MODEL_XEON_PHI_KNM      0x85

#define CPUID_MODEL_ATOM_SILVERMONT     0x37 // Bay Trail, Valleyview
#define CPUID_MODEL_ATOM_SILVERMONT_MID 0x4A // Merriefield
// "ATOM_SILVERMONT_X" is specified in, but not used by, the Linux kernel
// Disabled ATOM_SILVERMONT_X b/c it's documentation is strange; no use supporting an apparently non-existent CPU
// #define CPUID_MODEL_ATOM_SILVERMONT_X  0x4D // Avaton, Rangeley
#define CPUID_MODEL_ATOM_AIRMONT        0x4C // Cherry Trail, Braswell
#define CPUID_MODEL_ATOM_AIRMONT_MID    0x5A // Moorefield
// "SoFIA" does not appear to have Linux kernel support
#define CPUID_MODEL_ATOM_SOFIA          0x5D

#define CPUID_MODEL_ATOM_GOLDMONT       0x5C // Apollo Lake
#define CPUID_MODEL_ATOM_GOLDMONT_X     0x5F // Denverton
#define CPUID_MODEL_ATOM_GOLDMONT_PLUS  0x7A // Gemini Lake

#define CPUID_MODEL_ATOM_TREMONT_X      0x86 // Jacobsville
//----

/**
 * Check that the CPU vendor is GenuineIntel.
 *
 * @return 1 if Intel, 0 otherwise
 */
int cpuid_is_vendor_intel(void);

/**
 * Get the CPU family and model.
 * Model parsing assumes that family=6.
 *
 * @param family not NULL
 * @param model not NULL
 */
void cpuid_get_family_model(uint32_t* family, uint32_t* model);

/**
 * Check that family=6 and model is one of those listed above.
 *
 * @param family
 * @param model
 * @return 1 if supported, 0 otherwise
 */
int cpuid_is_cpu_supported(uint32_t family, uint32_t model);

#pragma GCC visibility pop

#ifdef __cplusplus
}
#endif

#endif

#ifndef _RAPLCAP_CPUID_H_
#define _RAPLCAP_CPUID_H_

#ifdef __cplusplus
extern "C" {
#endif

#pragma GCC visibility push(hidden)

#define CPUID_VENDOR_ID_GENUINE_INTEL "GenuineIntel"

/* 
 * See Software Developer's Manual, Volume 3
 * Last updated: September 2016
 * See: Table 35-1
 * See: Section 35.23 MSR INDEX (model listings don't account for inheritance through other tables though)
 *
 * See Linux kernel:
 * arch/x86/include/asm/intel-family.h
 * drivers/powercap/intel_rapl.c
 */

// Processors recognized and used in Linux kernel and Software Developer's Manual
//----
// Sandy Bridge is the first to support RAPL
#define CPUID_MODEL_SANDYBRIDGE       0x2A
#define CPUID_MODEL_SANDYBRIDGE_X     0x2D

#define CPUID_MODEL_IVYBRIDGE         0x3A
#define CPUID_MODEL_IVYBRIDGE_X       0x3E

#define CPUID_MODEL_HASWELL_CORE      0x3C
#define CPUID_MODEL_HASWELL_X         0x3F
#define CPUID_MODEL_HASWELL_ULT       0x45
#define CPUID_MODEL_HASWELL_GT3E      0x46

#define CPUID_MODEL_BROADWELL_CORE    0x3D
#define CPUID_MODEL_BROADWELL_GT3E    0x47
#define CPUID_MODEL_BROADWELL_X       0x4F
#define CPUID_MODEL_BROADWELL_XEON_D  0x56

#define CPUID_MODEL_SKYLAKE_MOBILE    0x4E
#define CPUID_MODEL_SKYLAKE_DESKTOP   0x5E
#define CPUID_MODEL_SKYLAKE_X         0x55

#define CPUID_MODEL_ATOM_SILVERMONT1  0x37
#define CPUID_MODEL_ATOM_AIRMONT      0x4C
#define CPUID_MODEL_ATOM_MERRIFIELD   0x4A
#define CPUID_MODEL_ATOM_MOOREFIELD   0x5A
#define CPUID_MODEL_ATOM_GOLDMONT     0x5C

#define CPUID_MODEL_XEON_PHI_KNL      0x57
//----

// Processors used in kernel but not documented in Software Developer's Manual
//----
#define CPUID_MODEL_KABYLAKE_MOBILE   0x8E
#define CPUID_MODEL_KABYLAKE_DESKTOP  0x9E

#define CPUID_MODEL_ATOM_DENVERTON    0x5F
#define CPUID_MODEL_ATOM_GEMINI_LAKE  0x7A

#define CPUID_MODEL_XEON_PHI_KNM      0x85
//----

// Processors specified in, but not used by, the kernel
// #define CPUID_MODEL_ATOM_SILVERMONT2  0x4D

// Processors not specified in the kernel but documented in Software Developer's Manual
// 0x5D (an Atom Silvermont processor)

/**
 * Check that the CPU vendor is GenuineIntel.
 *
 * @return 1 if Intel, 0 otherwise
 */
int raplcap_cpuid_is_vendor_intel(void);

/**
 * Get the CPU family and model.
 * Model parsing assumes that family=6.
 *
 * @param family not NULL
 * @param model not NULL
 */
void raplcap_cpuid_get_family_model(uint32_t* family, uint32_t* model);

/**
 * Check that family=6 and model is one of those listed above.
 *
 * @param family
 * @param model
 * @return 1 if supported, 0 otherwise
 */
int raplcap_cpuid_is_cpu_supported(uint32_t family, uint32_t model);

#pragma GCC visibility pop

#ifdef __cplusplus
}
#endif

#endif

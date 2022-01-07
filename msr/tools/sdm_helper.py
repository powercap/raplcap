#!/usr/bin/env python3
"""Helper script for processing RAPL data from Intel Software Developer's Manual, Volume 4."""

import sys

__author__ = "Connor Imes"
__date__ = "2018-04-19"


MSR_RAPL_POWER_UNIT = "MSR_RAPL_POWER_UNIT" # 0x606
MSR_PKG_POWER_LIMIT = "MSR_PKG_POWER_LIMIT" # 0x610
MSR_PKG_ENERGY_STATUS = "MSR_PKG_ENERGY_STATUS" # 0x611
MSR_PACKAGE_ENERGY_TIME_STATUS = "MSR_PACKAGE_ENERGY_TIME_STATUS" # 0x612
MSR_PP0_POWER_LIMIT = "MSR_PP0_POWER_LIMIT" # 0x638
MSR_PP0_ENERGY_STATUS = "MSR_PP0_ENERGY_STATUS" # 0x639
MSR_PP1_POWER_LIMIT = "MSR_PP1_POWER_LIMIT" # 0x640
MSR_PP1_ENERGY_STATUS = "MSR_PP1_ENERGY_STATUS" # 0x641
MSR_DRAM_POWER_LIMIT = "MSR_DRAM_POWER_LIMIT" # 0x618
MSR_DRAM_ENERGY_STATUS = "MSR_DRAM_ENERGY_STATUS" # 0x619
MSR_PLATFORM_POWER_LIMIT = "MSR_PLATFORM_POWER_LIMIT" # 0x65C
MSR_PLATFORM_ENERGY_COUNTER = "MSR_PLATFORM_ENERGY_COUNTER" # 0x64D
MSR_VR_CURRENT_CONFIG = "MSR_VR_CURRENT_CONFIG" # 0x601

NONE = "None"
POWER_DEFAULT = "14.10.1"
PKG_DEFAULT = "14.10.3"
PACKAGE_ENERGY_TIME_DEFAULT = "Table 2-47"
PP0_DEFAULT = "14.10.4"
PP1_DEFAULT = "14.10.4"
DRAM_DEFAULT = "14.10.5"
PLATFORM_DEFAULT = "Table 2-39"
DRAM_15_3 = "ESU: 15.3 uJ" # assumed for now that this ESU is found in MSR_RAPL_POWER_UNIT
DRAM_ICELAKE_XD = "Table 2-47"
RESERVED = "Reserved (0)" # this should also be OK (just gets a 0 energy reading)
PL4_DEFAULT = "Table 2-45"


class CPU(object):
    """A CPU with RAPL register info in a list of tables."""

    DELIM = ','

    REGS = [MSR_RAPL_POWER_UNIT,
            MSR_PKG_POWER_LIMIT, MSR_PKG_ENERGY_STATUS, MSR_PACKAGE_ENERGY_TIME_STATUS,
            MSR_PP0_POWER_LIMIT, MSR_PP0_ENERGY_STATUS,
            MSR_PP1_POWER_LIMIT, MSR_PP1_ENERGY_STATUS,
            MSR_DRAM_POWER_LIMIT, MSR_DRAM_ENERGY_STATUS,
            MSR_PLATFORM_POWER_LIMIT, MSR_PLATFORM_ENERGY_COUNTER,
            MSR_VR_CURRENT_CONFIG]

    def __init__(self, cpuid, name, register_dicts):
        self.cpuid = cpuid
        self.name = name
        self.registers = {}
        # Later tables override any MSR configs in earlier tables
        for rdic in register_dicts:
            self.registers.update(rdic)

    @staticmethod
    def print_header():
        """Print header."""
        sys.stdout.write('Model' + CPU.DELIM + 'Name')
        for reg in CPU.REGS:
            sys.stdout.write(CPU.DELIM + reg)
        sys.stdout.write('\n')

    def print_line(self):
        """Print line fpr CPU."""
        sys.stdout.write(self.cpuid + CPU.DELIM  + self.name)
        for reg in CPU.REGS:
            sys.stdout.write(CPU.DELIM + self.registers.get(reg, 'None'))
        sys.stdout.write('\n')


if __name__ == "__main__":
    # Exceptions to the SDM
    # These are not exhaustive, just the important ones that require coding changes in the library.
    # E.g., not specifying PP0 power limit exceptions which obviously exist---but are no documented---for all RAPL CPUs
    EXCEPTION_DRAM_ENERGY_STATUS_15_3 = {MSR_DRAM_ENERGY_STATUS: DRAM_15_3 + " [E]"}
    EXCEPTION_GOLDMONT_X = {MSR_RAPL_POWER_UNIT: POWER_DEFAULT + " [E]",
                            MSR_PKG_POWER_LIMIT: PKG_DEFAULT + " [E]",
                            MSR_PKG_ENERGY_STATUS: PKG_DEFAULT + " [E]",
                            MSR_DRAM_POWER_LIMIT: DRAM_DEFAULT + " [E]",
                            MSR_DRAM_ENERGY_STATUS: DRAM_DEFAULT + " [E]",
                            MSR_PP0_ENERGY_STATUS: PP0_DEFAULT + " [E]",
                            MSR_PP1_ENERGY_STATUS: PP1_DEFAULT + " [E]"}

    # Tables from the SDM
    CPU.print_header()

    TBL_6 = {}
    TBL_7 = {}
    TBL_8 = {MSR_RAPL_POWER_UNIT: "Table 2-8",
             MSR_PKG_POWER_LIMIT: "Table 2-8",
             MSR_PKG_ENERGY_STATUS: PKG_DEFAULT,
             MSR_PP0_ENERGY_STATUS: PP0_DEFAULT}
    TBL_9 = {}
    TBL_10 = {MSR_RAPL_POWER_UNIT: "Table 2-10",
              MSR_PKG_POWER_LIMIT: PKG_DEFAULT,
              MSR_PKG_ENERGY_STATUS: PKG_DEFAULT}
    TBL_11 = {MSR_PP0_POWER_LIMIT: "Table 2-11"}
    ATOM_SILVERMONT = CPU("0x37", "ATOM_SILVERMONT", [TBL_6, TBL_7, TBL_8, TBL_9])
    ATOM_SILVERMONT.print_line()
    ATOM_SILVERMONT_MID = CPU("0x4A", "ATOM_SILVERMONT_MID", [TBL_6, TBL_7, TBL_8])
    ATOM_SILVERMONT_MID.print_line()
    ATOM_SILVERMONT_D = CPU("0x4D", "ATOM_SILVERMONT_D", [TBL_6, TBL_7, TBL_10])
    ATOM_SILVERMONT_D.print_line()
    ATOM_AIRMONT_MID = CPU("0x5A", "ATOM_AIRMONT_MID", [TBL_6, TBL_7, TBL_8])
    ATOM_AIRMONT_MID.print_line()
    ATOM_SOFIA = CPU("0x5D", "ATOM_SOFIA", [TBL_6, TBL_7, TBL_8])
    ATOM_SOFIA.print_line()
    ATOM_AIRMONT = CPU("0x4C", "ATOM_AIRMONT", [TBL_6, TBL_7, TBL_8, TBL_11])
    ATOM_AIRMONT.print_line()

    TBL_12 = {MSR_RAPL_POWER_UNIT: POWER_DEFAULT,
              MSR_PKG_POWER_LIMIT: PKG_DEFAULT,
              MSR_PKG_ENERGY_STATUS: PKG_DEFAULT,
              MSR_DRAM_POWER_LIMIT: DRAM_DEFAULT,
              MSR_DRAM_ENERGY_STATUS: DRAM_DEFAULT,
              MSR_PP0_ENERGY_STATUS: PP0_DEFAULT,
              MSR_PP1_ENERGY_STATUS: PP1_DEFAULT}
    TBL_13 = {}
    ATOM_GOLDMONT = CPU("0x5C", "ATOM_GOLDMONT", [TBL_6, TBL_12])
    ATOM_GOLDMONT.print_line()
    # GOLDMONT_X (Denverton) not documented in SDM, but kernel uses standard RAPL conversions
    ATOM_GOLDMONT_X = CPU("0x5F", "ATOM_GOLDMONT_X", [EXCEPTION_GOLDMONT_X])
    ATOM_GOLDMONT_X.print_line()
    ATOM_GOLDMONT_PLUS = CPU("0x7A", "ATOM_GOLDMONT_PLUS", [TBL_6, TBL_12, TBL_13])
    ATOM_GOLDMONT_PLUS.print_line()

    TBL_14 = {}
    ATOM_TREMONT_X = CPU("0x86", "ATOM_TREMONT_X", [TBL_6, TBL_12, TBL_13, TBL_14])
    ATOM_TREMONT_X.print_line()

    TBL_20 = {MSR_RAPL_POWER_UNIT: POWER_DEFAULT,
              MSR_PKG_POWER_LIMIT: PKG_DEFAULT,
              MSR_PKG_ENERGY_STATUS: PKG_DEFAULT,
              MSR_PP0_POWER_LIMIT: PP0_DEFAULT}
    TBL_21 = {MSR_PP0_ENERGY_STATUS: PP0_DEFAULT,
              MSR_PP1_POWER_LIMIT: PP1_DEFAULT,
              MSR_PP1_ENERGY_STATUS: PP1_DEFAULT}
    TBL_22 = {}
    TBL_23 = {MSR_DRAM_POWER_LIMIT: DRAM_DEFAULT,
              MSR_DRAM_ENERGY_STATUS: DRAM_DEFAULT,
              MSR_PP0_ENERGY_STATUS: PP0_DEFAULT}
    TBL_24 = {}
    SANDYBRIDGE = CPU("0x2A", "SANDYBRIDGE", [TBL_20, TBL_21, TBL_22])
    SANDYBRIDGE.print_line()
    SANDYBRIDGE_X = CPU("0x2D", "SANDYBRIDGE_X", [TBL_20, TBL_23, TBL_24])
    SANDYBRIDGE_X.print_line()

    TBL_25 = {MSR_PP0_ENERGY_STATUS: PP0_DEFAULT}
    TBL_26 = {MSR_DRAM_POWER_LIMIT: DRAM_DEFAULT,
              MSR_DRAM_ENERGY_STATUS: DRAM_DEFAULT,
              MSR_PP0_ENERGY_STATUS: PP0_DEFAULT}
    TBL_27 = {}
    TBL_28 = {}
    IVYBRIDGE = CPU("0x3A", "IVYBRIDGE", [TBL_20, TBL_21, TBL_22, TBL_25])
    IVYBRIDGE.print_line()
    IVYBRIDGE_X = CPU("0x3E", "IVYBRIDGE_X", [TBL_20, TBL_24, TBL_26, TBL_27, TBL_28])
    IVYBRIDGE_X.print_line()

    TBL_29 = {MSR_DRAM_ENERGY_STATUS: DRAM_DEFAULT}
    TBL_30 = {MSR_RAPL_POWER_UNIT: POWER_DEFAULT,
              MSR_PP0_ENERGY_STATUS: PP0_DEFAULT,
              MSR_PP1_POWER_LIMIT: PP1_DEFAULT,
              MSR_PP1_ENERGY_STATUS: PP1_DEFAULT}
    TBL_31 = {}
    TBL_32 = {MSR_RAPL_POWER_UNIT: POWER_DEFAULT,
              MSR_DRAM_POWER_LIMIT: DRAM_DEFAULT,
              MSR_DRAM_ENERGY_STATUS: DRAM_15_3,
              MSR_PP0_ENERGY_STATUS: RESERVED}
    TBL_33 = {}
    # TBL_25 specified at end of TBL_30
    HASWELL = CPU("0x3C", "HASWELL", [TBL_20, TBL_21, TBL_22, TBL_25, TBL_29, TBL_30])
    HASWELL.print_line()
    HASWELL_X = CPU("0x3F", "HASWELL_X", [TBL_20, TBL_29, TBL_32, TBL_33])
    HASWELL_X.print_line()
    # TBL_22 specified at end of TBL_31
    HASWELL_L = CPU("0x45", "HASWELL_L", [TBL_20, TBL_21, TBL_22, TBL_29, TBL_30, TBL_31])
    HASWELL_L.print_line()
    # TBL_25 specified at end of TBL_30
    HASWELL_G = CPU("0x46", "HASWELL_G", [TBL_20, TBL_21, TBL_22, TBL_25, TBL_29, TBL_30])
    HASWELL_G.print_line()

    TBL_34 = {}
    TBL_35 = {MSR_PP0_ENERGY_STATUS: PP0_DEFAULT}
    TBL_36 = {MSR_RAPL_POWER_UNIT: POWER_DEFAULT,
              MSR_DRAM_POWER_LIMIT: DRAM_DEFAULT,
              MSR_DRAM_ENERGY_STATUS: DRAM_15_3,
              MSR_PP0_ENERGY_STATUS: RESERVED}
    TBL_37 = {}
    TBL_38 = {}
    BROADWELL = CPU("0x3D", "BROADWELL", [TBL_20, TBL_21, TBL_22, TBL_25, TBL_29, TBL_30, TBL_34, TBL_35])
    BROADWELL.print_line()
    BROADWELL_G = CPU("0x47", "BROADWELL_G", [TBL_20, TBL_21, TBL_22, TBL_25, TBL_29, TBL_30, TBL_34, TBL_35])
    BROADWELL_G.print_line()
    # BROADWELL_X: Section 2.16.2 specifies the prior tables for this architecture.
    # TODO: Also TBL_37? Mentioned at start of Section 2.16.2, but not included in explicit list
    # TODO: Comment at end of TBL_38 is for 0x45? Won't use (no effect on results anyway)...
    BROADWELL_X = CPU("0x4F", "BROADWELL_X", [TBL_20, TBL_21, TBL_29, TBL_34, TBL_36, TBL_38])
    BROADWELL_X.print_line()
    # BROADWELL_D: See 2.16.1 for mention of Tables 19 and 28
    # TODO: Discrepancy with kernel: it doesn't claim DRAM_15_3 for BROADWELL_D
    BROADWELL_D = CPU("0x56", "BROADWELL_D", [TBL_20, TBL_29, TBL_34, TBL_36, TBL_37])
    BROADWELL_D.print_line()

    TBL_39 = {MSR_PP0_ENERGY_STATUS: PP0_DEFAULT,
              MSR_PLATFORM_ENERGY_COUNTER: PLATFORM_DEFAULT,
              MSR_PLATFORM_POWER_LIMIT: PLATFORM_DEFAULT}
    TBL_40 = {}
    TBL_41 = {}
    TBL_42 = {}
    TBL_43 = {}
    TBL_44 = {}
    TBL_45 = {MSR_VR_CURRENT_CONFIG: PL4_DEFAULT}
    TBL_50 = {MSR_RAPL_POWER_UNIT: POWER_DEFAULT,
              MSR_DRAM_POWER_LIMIT: DRAM_DEFAULT,
              MSR_DRAM_ENERGY_STATUS: DRAM_15_3,
              MSR_PP0_ENERGY_STATUS: RESERVED}
    TBL_51 = {MSR_PACKAGE_ENERGY_TIME_STATUS: PACKAGE_ENERGY_TIME_DEFAULT,
              MSR_DRAM_POWER_LIMIT: DRAM_ICELAKE_XD,
              MSR_DRAM_ENERGY_STATUS: DRAM_15_3}
    SKYLAKE_L = CPU("0x4E", "SKYLAKE_L", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40])
    SKYLAKE_L.print_line()
    # Top of Section 2.17 says TBL_40 (Uncore) is used for 0x55, but TBL_40 doesn't mention it
    SKYLAKE_X = CPU("0x55", "SKYLAKE_X", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_50])
    SKYLAKE_X.print_line()
    SKYLAKE = CPU("0x5E", "SKYLAKE", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40])
    SKYLAKE.print_line()
    KABYLAKE_L = CPU("0x8E", "KABYLAKE_L", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_41])
    KABYLAKE_L.print_line()
    KABYLAKE = CPU("0x9E", "KABYLAKE", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_41])
    KABYLAKE.print_line()
    CANNONLAKE_L = CPU("0x66", "CANNONLAKE_L", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_42, TBL_43])
    CANNONLAKE_L.print_line()

    ICELAKE = CPU("0x7D", "ICELAKE", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_44])
    ICELAKE.print_line()
    ICELAKE_L = CPU("0x7E", "ICELAKE_L", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_44])
    ICELAKE_L.print_line()
    ICELAKE_X = CPU("0x6A", "ICELAKE_X", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_44, TBL_51])
    ICELAKE_X.print_line()
    ICELAKE_D = CPU("0x6C", "ICELAKE_D", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_44, TBL_51])
    ICELAKE_D.print_line()

    COMETLAKE = CPU("0xA5", "COMETLAKE", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39])
    COMETLAKE.print_line()
    COMETLAKE_L = CPU("0xA6", "COMETLAKE_L", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39])
    COMETLAKE_L.print_line()

    TIGERLAKE_L = CPU("0x8C", "TIGERLAKE_L", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_45])
    TIGERLAKE_L.print_line()
    TIGERLAKE = CPU("0x8D", "TIGERLAKE", [TBL_20, TBL_21, TBL_25, TBL_29, TBL_35, TBL_39, TBL_40, TBL_45])
    TIGERLAKE.print_line()

    TBL_53 = {MSR_RAPL_POWER_UNIT: POWER_DEFAULT,
              MSR_PKG_POWER_LIMIT: PKG_DEFAULT,
              MSR_PKG_ENERGY_STATUS: PKG_DEFAULT,
              MSR_DRAM_POWER_LIMIT: DRAM_DEFAULT,
              MSR_DRAM_ENERGY_STATUS: DRAM_DEFAULT, # community consensus is that Xeon Phi should be DRAM_15_3
              MSR_PP0_POWER_LIMIT: PP0_DEFAULT,
              MSR_PP0_ENERGY_STATUS: PP0_DEFAULT}
    TBL_54 = {}
    # The SDM and Xeon Phi Processor Datasheets (Vol. 2) don't back up this configuration
    # However, the community consensus is that Xeon Phi CPUs use 15.3 uJ as the DRAM energy units
    XEON_PHI_KNL = CPU("0x57", "XEON_PHI_KNL", [TBL_53, EXCEPTION_DRAM_ENERGY_STATUS_15_3])
    XEON_PHI_KNL.print_line()
    XEON_PHI_KNM = CPU("0x85", "XEON_PHI_KNM", [TBL_53, TBL_54, EXCEPTION_DRAM_ENERGY_STATUS_15_3])
    XEON_PHI_KNM.print_line()

    # Last updated for Software Developer's Manual, Volume 4 - April 2021

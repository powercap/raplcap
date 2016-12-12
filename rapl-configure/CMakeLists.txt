# Binaries

add_executable(rapl-configure-msr rapl-configure.c)
target_link_libraries(rapl-configure-msr raplcap-msr)
install(TARGETS rapl-configure-msr DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")

if(POWERCAP_FOUND)
  add_executable(rapl-configure-sysfs rapl-configure.c)
  target_link_libraries(rapl-configure-sysfs raplcap-sysfs)
  install(TARGETS rapl-configure-sysfs DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()

if(libmsr_FOUND)
  add_executable(rapl-configure-libmsr rapl-configure.c)
  # target_link_libraries(rapl-configure-libmsr -L${libmsr_LIB_PATH} msr)
  # Must specify transitive libm dependency if forcing static libmsr dependency
  # target_link_libraries(rapl-configure-libmsr -L${libmsr_LIB_PATH} msr.a m)
  target_link_libraries(rapl-configure-libmsr raplcap-libmsr)
  install(TARGETS rapl-configure-libmsr DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()

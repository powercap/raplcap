# Binaries

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(RAPL_LIB "msr")
  add_executable(rapl-configure-${RAPL_LIB} rapl-configure.c)
  target_include_directories(rapl-configure-${RAPL_LIB} PRIVATE ../msr)
  target_compile_definitions(rapl-configure-${RAPL_LIB} PRIVATE RAPLCAP_${RAPL_LIB})
  target_link_libraries(rapl-configure-${RAPL_LIB} raplcap-${RAPL_LIB})
  install(TARGETS rapl-configure-${RAPL_LIB} DESTINATION ${CMAKE_INSTALL_BINDIR})

  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/rapl-configure.1.in
    ${CMAKE_CURRENT_BINARY_DIR}/man/man1/rapl-configure-${RAPL_LIB}.1
    @ONLY
  )
endif()

if(POWERCAP_FOUND)
  set(RAPL_LIB "powercap")
  add_executable(rapl-configure-${RAPL_LIB} rapl-configure.c)
  target_link_libraries(rapl-configure-${RAPL_LIB} raplcap-${RAPL_LIB})
  install(TARGETS rapl-configure-${RAPL_LIB} DESTINATION ${CMAKE_INSTALL_BINDIR})

  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/rapl-configure.1.in
    ${CMAKE_CURRENT_BINARY_DIR}/man/man1/rapl-configure-${RAPL_LIB}.1
    @ONLY
  )
endif()

if(libmsr_FOUND)
  set(RAPL_LIB "libmsr")
  add_executable(rapl-configure-${RAPL_LIB} rapl-configure.c)
  # target_link_libraries(rapl-configure-${RAPL_LIB} -L${libmsr_LIB_PATH} msr)
  # Must specify transitive libm dependency if forcing static libmsr dependency
  # target_link_libraries(rapl-configure-${RAPL_LIB} -L${libmsr_LIB_PATH} msr.a m)
  target_link_libraries(rapl-configure-${RAPL_LIB} raplcap-${RAPL_LIB})
  install(TARGETS rapl-configure-${RAPL_LIB} DESTINATION ${CMAKE_INSTALL_BINDIR})

  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/rapl-configure.1.in
    ${CMAKE_CURRENT_BINARY_DIR}/man/man1/rapl-configure-${RAPL_LIB}.1
    @ONLY
  )
endif()

if(IntelPowerGadgetLib OR WIN32)
  set(RAPL_LIB "ipg")
  add_executable(rapl-configure-${RAPL_LIB} rapl-configure.c)
  target_link_libraries(rapl-configure-${RAPL_LIB} raplcap-${RAPL_LIB})
  install(TARGETS rapl-configure-${RAPL_LIB} DESTINATION ${CMAKE_INSTALL_BINDIR})

  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/rapl-configure.1.in
    ${CMAKE_CURRENT_BINARY_DIR}/man/man1/rapl-configure-${RAPL_LIB}.1
    @ONLY
  )
endif()

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/man/ DESTINATION ${CMAKE_INSTALL_MANDIR} OPTIONAL)

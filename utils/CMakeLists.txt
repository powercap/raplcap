# Binaries

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(RAPL_LIB "msr")
  add_executable(raplcap-poll-${RAPL_LIB} raplcap-poll.c)
  target_include_directories(raplcap-poll-${RAPL_LIB} PRIVATE ../msr)
  target_link_libraries(raplcap-poll-${RAPL_LIB} PRIVATE raplcap-${RAPL_LIB})
  install(TARGETS raplcap-poll-${RAPL_LIB} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(Powercap_FOUND)
  set(RAPL_LIB "powercap")
  add_executable(raplcap-poll-${RAPL_LIB} raplcap-poll.c)
  target_link_libraries(raplcap-poll-${RAPL_LIB} PRIVATE raplcap-${RAPL_LIB})
  install(TARGETS raplcap-poll-${RAPL_LIB} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

cmake_minimum_required(VERSION 3.16)
project(adr_prototype C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

enable_testing()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(SQLITE3_EXECUTABLE sqlite3)

file(COPY ${CMAKE_SOURCE_DIR}/src/adr_cli/adr.py
     DESTINATION ${CMAKE_BINARY_DIR})

if(UNIX AND NOT APPLE)
  add_library(adr_common STATIC
    src/common/hash.c
    src/common/util.c
  )
  target_include_directories(adr_common PUBLIC ${CMAKE_SOURCE_DIR}/src)

  add_executable(adr-recorder
    recorder/ptrace_recorder.c
  )
  target_link_libraries(adr-recorder PRIVATE adr_common)

  add_executable(adr-replayer
    replayer/ptrace_replayer.c
  )
  target_link_libraries(adr-replayer PRIVATE adr_common)

  add_library(adr_sync_hook SHARED
    hooks/sync_hook.c
  )
  target_link_libraries(adr_sync_hook PRIVATE dl pthread adr_common)
  set_target_properties(adr_sync_hook PROPERTIES OUTPUT_NAME adr_sync_hook)

  add_executable(time_demo tests/programs/time_demo/time_demo.c)
  add_executable(random_demo tests/programs/random_demo/random_demo.c)
  add_executable(read_file_demo tests/programs/read_file_demo/read_file_demo.c)
  add_executable(stdout_demo tests/programs/stdout_demo/stdout_demo.c)
  add_executable(drift_demo tests/programs/drift_demo/drift_demo.c)
  add_executable(mutex_counter tests/programs/mutex_counter/mutex_counter.c)
  target_link_libraries(mutex_counter PRIVATE pthread)
  add_executable(race_counter tests/programs/race_counter/race_counter.c)
  target_link_libraries(race_counter PRIVATE pthread)
  add_executable(deadlock_demo tests/programs/deadlock_demo/deadlock_demo.c)
  target_link_libraries(deadlock_demo PRIVATE pthread)

  set_target_properties(time_demo random_demo read_file_demo stdout_demo drift_demo
    mutex_counter race_counter deadlock_demo
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/adr
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/adr_cli/adr.py ${CMAKE_BINARY_DIR}/adr
    COMMAND chmod +x ${CMAKE_BINARY_DIR}/adr
    DEPENDS ${CMAKE_SOURCE_DIR}/src/adr_cli/adr.py
  )
  add_custom_target(adr_cli ALL DEPENDS ${CMAKE_BINARY_DIR}/adr)

  add_test(NAME cli_help COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/adr_cli/adr.py --help)
  add_test(NAME time_demo_pipeline
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/adr_cli/adr.py run
      --target ${CMAKE_BINARY_DIR}/tests/time_demo
      --trace-root ${CMAKE_BINARY_DIR}/traces
      --report ${CMAKE_BINARY_DIR}/reports/time_demo.md
      --repeat 1)

  if(SQLITE3_EXECUTABLE)
    add_test(NAME sqlite_smoke_pipeline
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/adr_cli/adr.py run
        --target ${CMAKE_SOURCE_DIR}/scripts/sqlite_smoke.sh
        --trace-root ${CMAKE_BINARY_DIR}/traces
        --report ${CMAKE_BINARY_DIR}/reports/sqlite_smoke.md
        --repeat 1)
  endif()
else()
  message(WARNING "Low-level ADR targets require Linux x86-64. Only Python CLI files are copied on this platform.")
  add_test(NAME cli_help COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/src/adr_cli/adr.py --help)
endif()

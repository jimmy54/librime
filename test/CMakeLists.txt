aux_source_directory(. rime_test_src)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/test)
add_executable(rime_test ${rime_test_src})
if(TARGET GTest::gtest)
  get_target_property(_gtest_include_dir GTest::gtest
    INTERFACE_INCLUDE_DIRECTORIES)
  if(_gtest_include_dir STREQUAL "${PROJECT_SOURCE_DIR}/include")
    # with the bundled gtest, the imported target marks the bundled include/
    # dir as a system include; gtest headers from the system (e.g. Homebrew)
    # would then shadow the bundled ones and break the ABI against
    # libgtest.a, so keep the bundled headers as a regular include dir.
    set_target_properties(GTest::gtest PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES ""
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "")
  endif()
endif()
target_link_libraries(rime_test
  ${rime_library}
  ${rime_dict_library}
  ${rime_gears_library}
  ${GTEST_LIBRARIES})
if(BUILD_SHARED_LIBS)
  target_compile_definitions(rime_test PRIVATE RIME_IMPORTS)
endif(BUILD_SHARED_LIBS)
if(TARGET rime-clmg-objs)
  target_include_directories(rime_test PRIVATE
    ${PROJECT_SOURCE_DIR}/plugins/clmg/src)
  target_compile_definitions(rime_test PRIVATE RIME_TEST_CLMG)

  add_executable(rime_clmg_scheme_test
    clmg_scheme/clmg_scheme_main.cc
    clmg_scheme/clmg_scheme_test.cc)
  target_compile_definitions(rime_clmg_scheme_test PRIVATE
    RIME_TEST_CLMG
    RIME_CLMG_CASES_PATH="${PROJECT_SOURCE_DIR}/tools/eval/cases/test.tsv")
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(rime_clmg_scheme_test PRIVATE RIME_IMPORTS)
  endif(BUILD_SHARED_LIBS)
  target_link_libraries(rime_clmg_scheme_test
    ${rime_library}
    ${rime_dict_library}
    ${rime_gears_library}
    ${GTEST_LIBRARIES})
  add_test(NAME rime_clmg_scheme_test
    COMMAND rime_clmg_scheme_test
    WORKING_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})
endif()
if(NOT Opencc_DICT_DIR)
  message(FATAL_ERROR "Could not locate the installed opencc dict directory "
                       "(e.g. t2s.json); required to build rime_test.")
endif()
target_compile_definitions(rime_test PRIVATE
  RIME_OPENCC_DICT_DIR="${Opencc_DICT_DIR}")

file(GLOB test_data_files ${PROJECT_SOURCE_DIR}/data/test/*.yaml)
file(COPY ${test_data_files} DESTINATION ${EXECUTABLE_OUTPUT_PATH})

add_test(NAME rime_test
  COMMAND rime_test
  WORKING_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

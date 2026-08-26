add_executable(OpenSearchParserTest
  OpenSearchParserTest.cpp
  ${REPO_ROOT}/lib/OpdsParser/OpenSearchParser.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(OpenSearchParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs  # no-op Logging.h (real one needs Arduino)
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(OpenSearchParserTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

target_link_libraries(OpenSearchParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpenSearchParserTest)

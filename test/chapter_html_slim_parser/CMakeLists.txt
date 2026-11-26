# Everything buildDictionaryHtmlPages links against, shared by both executables.
set(STYLED_LAYOUT_SOURCES
  ParserLinkStubs.cpp
  ${REPO_ROOT}/src/util/DictHtmlPages.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/CjkBreaks.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/WordStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

add_executable(ChapterHtmlSlimParserTest
  ChapterHtmlSlimParserTest.cpp
  DictionaryHtmlPagesTest.cpp
  ${STYLED_LAYOUT_SOURCES}
)

# The same layout as the PSRAM boards build it.
add_executable(DictionaryHtmlPagesPsramTest
  DictionaryHtmlPagesPsramTest.cpp
  ${STYLED_LAYOUT_SOURCES}
)
target_compile_definitions(DictionaryHtmlPagesPsramTest PRIVATE FREEINK_DEVICE_X4PRO=1)

foreach(target ChapterHtmlSlimParserTest DictionaryHtmlPagesPsramTest)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${REPO_ROOT}/src
    ${REPO_ROOT}/lib
    ${REPO_ROOT}/lib/Epub
    ${REPO_ROOT}/lib/Epub/Epub
    ${REPO_ROOT}/lib/Epub/Epub/parsers
    ${REPO_ROOT}/lib/Epub/Epub/css
    ${REPO_ROOT}/lib/AsciiText
    ${REPO_ROOT}/lib/EpdFont
    ${REPO_ROOT}/lib/Utf8
    ${REPO_ROOT}/lib/Memory
    ${REPO_ROOT}/lib/Serialization
    ${REPO_ROOT}/lib/MiniBidi
    ${REPO_ROOT}/lib/FsHelpers
    ${REPO_ROOT}/lib/XmlParserUtils
  )

  target_link_libraries(${target} PRIVATE
    crosspoint_test_common
    GTest::gtest_main
    expat
  )

  gtest_discover_tests(${target})
endforeach()

target_include_directories(DictionaryHtmlPagesPsramTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs_psram
  ${REPO_ROOT}/lib/hal
)


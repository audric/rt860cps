cmake_minimum_required(VERSION 3.21)
set(RT860_VERSION "0.1.1" CACHE STRING "Version of this build (release builds pass the tag)")
project(rt860cps VERSION ${RT860_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Test)
include(GNUInstallDirs)

# Where the shipped lists are installed, relative to the program: Linux uses
# bin/ and share/, the Windows zip keeps everything in one folder.
if(WIN32)
  set(RT860_LISTS_DEST lists)
  set(RT860_LISTS_FROM_BIN lists)
else()
  set(RT860_LISTS_DEST ${CMAKE_INSTALL_DATADIR}/rt860cps/lists)
  set(RT860_LISTS_FROM_BIN ../${CMAKE_INSTALL_DATADIR}/rt860cps/lists)
endif()

add_library(rt860core STATIC
  src/core/protocol.cpp src/core/protocol.h
  src/core/layout.cpp src/core/layout.h src/core/defaults.inc
  src/core/presets.cpp src/core/presets.h
  src/core/sync.cpp src/core/sync.h
  src/core/serialtransport.h)
if(WIN32)
  target_sources(rt860core PRIVATE src/core/serialtransport_win.cpp)
else()
  target_sources(rt860core PRIVATE src/core/serialtransport.cpp)
endif()
target_include_directories(rt860core PUBLIC src/core)
target_link_libraries(rt860core PUBLIC Qt6::Core)

add_executable(rt860cps WIN32
  src/main.cpp
  src/gui/channelmodel.cpp src/gui/channelmodel.h
  src/gui/settingspanel.cpp src/gui/settingspanel.h
  src/gui/zonepanel.cpp src/gui/zonepanel.h
  src/gui/mainwindow.cpp src/gui/mainwindow.h)
target_link_libraries(rt860cps PRIVATE rt860core Qt6::Widgets)
target_compile_definitions(rt860cps PRIVATE RT860_VERSION="${PROJECT_VERSION}"
  RT860_LISTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lists" RT860_LISTS_FROM_BIN="${RT860_LISTS_FROM_BIN}")

enable_testing()
add_executable(test_core tests/test_core.cpp tests/fakeradio.h)
target_link_libraries(test_core PRIVATE rt860core Qt6::Test)
target_include_directories(test_core PRIVATE tests)
target_compile_definitions(test_core PRIVATE RT860_LISTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lists")
add_test(NAME core COMMAND test_core)

# ---- install and packages (Linux: cpack -G "DEB;TGZ", Windows: cpack -G ZIP)
if(WIN32)
  # One folder that runs as it is: the program, Qt, the compiler's runtime and the lists.
  install(TARGETS rt860cps RUNTIME DESTINATION .)
  install(DIRECTORY lists/ DESTINATION ${RT860_LISTS_DEST})
  install(FILES README.md DESTINATION .)
  install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
  install(FILES $<TARGET_FILE:Qt6::Core> $<TARGET_FILE:Qt6::Gui> $<TARGET_FILE:Qt6::Widgets> DESTINATION .)
  install(FILES $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin> DESTINATION platforms)
  if(TARGET Qt6::QModernWindowsStylePlugin)
    install(FILES $<TARGET_FILE:Qt6::QModernWindowsStylePlugin> DESTINATION styles)
  elseif(TARGET Qt6::QWindowsVistaStylePlugin)
    install(FILES $<TARGET_FILE:Qt6::QWindowsVistaStylePlugin> DESTINATION styles)
  endif()
  if(MINGW)
    foreach(dll libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
      execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${dll}
        OUTPUT_VARIABLE dll_path OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(NOT IS_ABSOLUTE "${dll_path}")
        find_file(RT860_${dll} ${dll} REQUIRED)   # some toolchains keep it on the search path only
        set(dll_path ${RT860_${dll}})
      endif()
      install(FILES ${dll_path} DESTINATION .)
    endforeach()
  endif()
else()
  install(TARGETS rt860cps RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/rt860cps.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(DIRECTORY lists/ DESTINATION ${RT860_LISTS_DEST})
  install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

find_program(DPKG dpkg)
if(DPKG)
  execute_process(COMMAND ${DPKG} --print-architecture OUTPUT_VARIABLE K5_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(K5_ARCH ${CMAKE_SYSTEM_PROCESSOR})
endif()
set(CPACK_PACKAGE_NAME rt860cps)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Channel, zone and settings editor for the Radtel RT-860")
set(CPACK_PACKAGE_CONTACT "rt860cps maintainers <https://github.com/audric/rt860cps>")
if(WIN32)
  set(CPACK_PACKAGE_FILE_NAME "rt860cps-${PROJECT_VERSION}-windows-x64")
else()
  set(CPACK_PACKAGE_FILE_NAME "rt860cps-${PROJECT_VERSION}-linux-${K5_ARCH}")
endif()
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION hamradio)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/audric/rt860cps")
include(CPack)

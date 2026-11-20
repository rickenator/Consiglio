cmake_minimum_required(VERSION 3.16)
project(ConsiglioHermes VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Sources are UTF-8 (UI strings use …, ·, —); MSVC needs to be told.
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Svg Test)

# ─── ACP backend + command catalog (no widgets; shared by app and tests) ───
add_library(HermesAcp STATIC
    src/acpclient.cpp
    src/acpclient.h
    src/commandcatalog.cpp
    src/commandcatalog.h
    src/hermessetup.cpp
    src/hermessetup.h
)
target_include_directories(HermesAcp PUBLIC src)
target_compile_definitions(HermesAcp PUBLIC CONSIGLIO_HERMES_VERSION="${PROJECT_VERSION}")
target_link_libraries(HermesAcp PUBLIC Qt5::Core)

# ─── UI library (everything but main(); linked by the app, GUI tests and the
#     screenshot tour) ─────────────────────────────────────────────────────
add_library(HermesUi STATIC
    resources/hermes.qrc
    src/uiutil.cpp
    src/uiutil.h
    src/theme.cpp
    src/theme.h
    src/chatpane.cpp
    src/chatpane.h
    src/commandpalette.cpp
    src/commandpalette.h
    src/commanddialog.cpp
    src/commanddialog.h
    src/hermescli.cpp
    src/hermescli.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/sessionrail.cpp
    src/sessionrail.h
    src/filepicker.cpp
    src/filepicker.h
    src/fontsettings.cpp
    src/fontsettings.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/hermessetupdialog.cpp
    src/hermessetupdialog.h
)
target_include_directories(HermesUi PUBLIC src)
target_link_libraries(HermesUi PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Svg HermesAcp)

# ─── Main application ──────────────────────────────────────────────────────
set(APP_PLATFORM_SOURCES)
if(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE ConsiglioHermes.icns)
    set(APP_PLATFORM_SOURCES resources/ConsiglioHermes.icns)
    set_source_files_properties(resources/ConsiglioHermes.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
    set(APP_PLATFORM_SOURCES resources/ConsiglioHermes.rc)
endif()
add_executable(ConsiglioHermes WIN32 MACOSX_BUNDLE src/main.cpp ${APP_PLATFORM_SOURCES})
target_link_libraries(ConsiglioHermes PRIVATE HermesUi)
set_target_properties(ConsiglioHermes PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Consiglio Hermes"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.rickenator.consiglio.hermes"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Consiglio contributors")

# ─── Install + packaging ───────────────────────────────────────────────────
# Only the client is installed. The packages carry no maintainer scripts:
# nothing installs, updates or touches Hermes Agent or ~/.hermes at install
# time. Hermes is detected (read-only) on first launch and the user decides.
include(GNUInstallDirs)
if(APPLE)
    install(TARGETS ConsiglioHermes BUNDLE DESTINATION .)
else()
    install(TARGETS ConsiglioHermes RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(UNIX AND NOT APPLE)
    install(FILES resources/consiglio-hermes.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/logo.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME consiglio-hermes.svg)
    install(FILES resources/consiglio-hermes.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()

set(CPACK_PACKAGE_NAME "consiglio-hermes")
set(CPACK_PACKAGE_VENDOR "Consiglio")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native desktop client for the Hermes Agent")
set(CPACK_PACKAGE_CONTACT "Consiglio <rickenator@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/rickenator/Consiglio")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_STRIP_FILES ON)
include(CPack)

# ─── Tests ─────────────────────────────────────────────────────────────────
enable_testing()
set(FAKE_ACP "${CMAKE_CURRENT_SOURCE_DIR}/tests/fake_hermes_acp.py")

add_executable(test_acpclient tests/test_acpclient.cpp)
target_link_libraries(test_acpclient PRIVATE Qt5::Core Qt5::Test HermesAcp)
target_compile_definitions(test_acpclient PRIVATE FAKE_ACP_SCRIPT="${FAKE_ACP}")
add_test(NAME AcpClientTests COMMAND test_acpclient)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_commandcatalog.cpp")
    add_executable(test_commandcatalog tests/test_commandcatalog.cpp)
    target_link_libraries(test_commandcatalog PRIVATE Qt5::Test HermesUi)
    target_compile_definitions(test_commandcatalog PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME CommandCatalogTests COMMAND test_commandcatalog)
    set_tests_properties(CommandCatalogTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mainwindow.cpp")
    add_executable(test_mainwindow tests/test_mainwindow.cpp)
    target_link_libraries(test_mainwindow PRIVATE Qt5::Test HermesUi)
    target_compile_definitions(test_mainwindow PRIVATE FAKE_ACP_SCRIPT="${FAKE_ACP}")
    add_test(NAME MainWindowTests COMMAND test_mainwindow)
    set_tests_properties(MainWindowTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_executable(test_hermessetup tests/test_hermessetup.cpp)
target_link_libraries(test_hermessetup PRIVATE Qt5::Test HermesUi)
add_test(NAME HermesSetupTests COMMAND test_hermessetup)
set_tests_properties(HermesSetupTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The ACP and main-window suites drive the fake agent through a POSIX shell
# wrapper, so they are labelled and skipped on Windows CI.
set_tests_properties(AcpClientTests PROPERTIES LABELS posix)
if(TARGET test_mainwindow)
    set_tests_properties(MainWindowTests PROPERTIES LABELS posix)
endif()

add_executable(setuptour tests/setuptour.cpp)
target_link_libraries(setuptour PRIVATE Qt5::Test HermesUi)

# Offscreen screenshot tour (not a test; run manually / in CI artifacts).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/uitour.cpp")
    add_executable(uitour tests/uitour.cpp)
    target_link_libraries(uitour PRIVATE Qt5::Test HermesUi)
    target_compile_definitions(uitour PRIVATE FAKE_ACP_SCRIPT="${FAKE_ACP}")
endif()

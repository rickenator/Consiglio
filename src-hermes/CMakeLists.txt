cmake_minimum_required(VERSION 3.16)
project(ConsiglioHermes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Svg Test)

# ─── ACP backend + command catalog (no widgets; shared by app and tests) ───
add_library(HermesAcp STATIC
    src/acpclient.cpp
    src/acpclient.h
    src/commandcatalog.cpp
    src/commandcatalog.h
)
target_include_directories(HermesAcp PUBLIC src)
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
)
target_include_directories(HermesUi PUBLIC src)
target_link_libraries(HermesUi PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Svg HermesAcp)

# ─── Main application ──────────────────────────────────────────────────────
add_executable(ConsiglioHermes src/main.cpp)
target_link_libraries(ConsiglioHermes PRIVATE HermesUi)

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

# Offscreen screenshot tour (not a test; run manually / in CI artifacts).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/uitour.cpp")
    add_executable(uitour tests/uitour.cpp)
    target_link_libraries(uitour PRIVATE Qt5::Test HermesUi)
    target_compile_definitions(uitour PRIVATE FAKE_ACP_SCRIPT="${FAKE_ACP}")
endif()

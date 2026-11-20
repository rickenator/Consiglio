cmake_minimum_required(VERSION 3.16)
project(ConsiglioHermes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Test)

# ─── ACP backend library (shared by app and tests) ────────────────────────
add_library(HermesAcp STATIC
    src/acpclient.cpp
    src/acpclient.h
)
target_include_directories(HermesAcp PUBLIC src)
target_link_libraries(HermesAcp PUBLIC Qt5::Core)

# ─── Main application ──────────────────────────────────────────────────────
add_executable(ConsiglioHermes
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/chatpane.cpp
    src/chatpane.h
    src/commandpalette.cpp
    src/commandpalette.h
    src/filepicker.cpp
    src/filepicker.h
    src/fontsettings.cpp
    src/fontsettings.h
    src/fontsettingsdialog.cpp
    src/fontsettingsdialog.h
    src/sessionrail.cpp
    src/sessionrail.h
)
target_include_directories(ConsiglioHermes PRIVATE src)
target_link_libraries(ConsiglioHermes PRIVATE
    Qt5::Core
    Qt5::Widgets
    HermesAcp
)

# ─── Tests ─────────────────────────────────────────────────────────────────
enable_testing()

add_executable(test_acpclient tests/test_acpclient.cpp)
target_include_directories(test_acpclient PRIVATE src)
target_link_libraries(test_acpclient PRIVATE Qt5::Core Qt5::Test HermesAcp)
target_compile_definitions(test_acpclient PRIVATE
    FAKE_ACP_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/tests/fake_hermes_acp.py")
add_test(NAME AcpClientTests COMMAND test_acpclient)

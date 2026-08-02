cmake_minimum_required(VERSION 3.16)
project(Consiglio VERSION 0.2.0 LANGUAGES CXX)

configure_file(mcp/consiglio_history_mcp.py
               ${CMAKE_CURRENT_BINARY_DIR}/consiglio_history_mcp.py COPYONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network Concurrent Sql Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ─── Backend Library (shared by app and tests) ──────────────────────────────
set(BACKEND_SOURCES
    src/backend/appdatabase.cpp
    src/backend/agentdetector.cpp
    src/backend/approvalrouter.cpp
    src/backend/ptyhandler.cpp
    src/backend/sessionmanager.cpp
    src/backend/settings.cpp
)

add_library(ConsiglioBackend STATIC ${BACKEND_SOURCES})
target_include_directories(ConsiglioBackend PUBLIC src)
target_link_libraries(ConsiglioBackend PUBLIC Qt5::Core Qt5::Network Qt5::Sql)
if(UNIX AND NOT ANDROID)
    target_link_libraries(ConsiglioBackend PUBLIC util)
endif()

# ─── Main Application ───────────────────────────────────────────────────────
set(QT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/sidebar.cpp
    src/sidebar.h
    src/panelmanager.cpp
    src/panelmanager.h
    src/startupwizard.cpp
    src/startupwizard.h
    src/settingsdialog.cpp
    src/settingsdialog.h
    src/providerselectiondialog.cpp
    src/providerselectiondialog.h
    src/newsessiondialog.cpp
    src/newsessiondialog.h
    src/sessionlist.cpp
    src/sessionlist.h
    src/eventtimeline.cpp
    src/eventtimeline.h
    src/filebrowser.cpp
    src/filebrowser.h
    src/secretsmanager.cpp
    src/secretsmanager.h
    src/mobilepairing.cpp
    src/mobilepairing.h
    src/projectpanel.cpp
    src/projectpanel.h
    src/approvaldialog.cpp
    src/approvaldialog.h
    src/models/sessionmodel.cpp
    src/models/eventmodel.cpp
    resources/icons.qrc
)

add_executable(Consiglio ${QT_SOURCES})

target_include_directories(Consiglio PRIVATE src)

target_link_libraries(Consiglio PRIVATE
    Qt5::Core
    Qt5::Widgets
    Qt5::Network
    Qt5::Concurrent
    Qt5::Sql
    ConsiglioBackend
)

# ─── Tests (one executable per test class) ──────────────────────────────────
enable_testing()

add_executable(test_approvalrouter tests/test_approvalrouter.cpp)
target_link_libraries(test_approvalrouter PRIVATE Qt5::Core Qt5::Test ConsiglioBackend)
add_test(NAME ApprovalRouterTests COMMAND test_approvalrouter)

add_executable(test_settings tests/test_settings.cpp)
target_link_libraries(test_settings PRIVATE Qt5::Core Qt5::Test ConsiglioBackend)
add_test(NAME SettingsTests COMMAND test_settings)

add_executable(test_sessionmanager tests/test_sessionmanager.cpp)
target_link_libraries(test_sessionmanager PRIVATE Qt5::Core Qt5::Test ConsiglioBackend)
add_test(NAME SessionManagerTests COMMAND test_sessionmanager)

add_executable(test_ptyhandler tests/test_ptyhandler.cpp)
target_link_libraries(test_ptyhandler PRIVATE Qt5::Core Qt5::Test ConsiglioBackend)
add_test(NAME PtyHandlerTests COMMAND test_ptyhandler)

add_executable(test_agentdetector tests/test_agentdetector.cpp)
target_link_libraries(test_agentdetector PRIVATE Qt5::Core Qt5::Test ConsiglioBackend)
add_test(NAME AgentDetectorTests COMMAND test_agentdetector)

add_executable(test_providerselectiondialog
    tests/test_providerselectiondialog.cpp
    src/providerselectiondialog.cpp
    src/providerselectiondialog.h
)
target_include_directories(test_providerselectiondialog PRIVATE src)
target_link_libraries(test_providerselectiondialog PRIVATE
    Qt5::Core Qt5::Widgets Qt5::Network Qt5::Test ConsiglioBackend)
add_test(NAME ProviderSelectionDialogTests COMMAND test_providerselectiondialog)

add_executable(test_eventtimeline
    tests/test_eventtimeline.cpp
    src/eventtimeline.cpp
    src/eventtimeline.h
)
target_include_directories(test_eventtimeline PRIVATE src)
target_link_libraries(test_eventtimeline PRIVATE
    Qt5::Core Qt5::Widgets Qt5::Test ConsiglioBackend)
add_test(NAME EventTimelineTests COMMAND test_eventtimeline)

add_executable(test_settingsdialog
    tests/test_settingsdialog.cpp
    src/settingsdialog.cpp
    src/settingsdialog.h
)
target_include_directories(test_settingsdialog PRIVATE src)
target_link_libraries(test_settingsdialog PRIVATE
    Qt5::Core Qt5::Widgets Qt5::Network Qt5::Test ConsiglioBackend)
add_test(NAME SettingsDialogTests COMMAND test_settingsdialog)

add_executable(test_appdatabase tests/test_appdatabase.cpp)
target_include_directories(test_appdatabase PRIVATE src)
target_link_libraries(test_appdatabase PRIVATE
    Qt5::Core Qt5::Sql Qt5::Test ConsiglioBackend)
add_test(NAME AppDatabaseTests COMMAND test_appdatabase)

add_test(NAME ConsiglioHistoryMcpTests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_consiglio_history_mcp.py
            ${CMAKE_CURRENT_BINARY_DIR}/consiglio_history_mcp.py)

# Install
install(TARGETS Consiglio BUNDLE DESTINATION .)
install(PROGRAMS mcp/consiglio_history_mcp.py DESTINATION bin)

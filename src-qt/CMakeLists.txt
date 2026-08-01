cmake_minimum_required(VERSION 3.16)
project(Consiglio VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets Network Concurrent Sql)

# ─── Sources ──────────────────────────────────────────────────────────────────
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
    src/discussionpanel.cpp
    src/discussionpanel.h
    src/approvaldialog.cpp
    src/approvaldialog.h
    src/backend/agentdetector.cpp
    src/backend/agentdetector.h
    src/backend/sessionmanager.cpp
    src/backend/sessionmanager.h
    src/backend/settings.cpp
    src/backend/settings.h
    src/backend/approvalrouter.cpp
    src/backend/approvalrouter.h
    src/backend/ptyhandler.cpp
    src/backend/ptyhandler.h
    src/models/sessionmodel.cpp
    src/models/sessionmodel.h
    src/models/eventmodel.cpp
    src/models/eventmodel.h
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
)

# Install
install(TARGETS Consiglio BUNDLE DESTINATION .)

cmake_minimum_required(VERSION 3.21)

project(kooldock2 VERSION 0.5.0 LANGUAGES CXX C)

set(PROJECT_BUG_ADDRESS "https://github.com/anomalyco/kooldock2/issues")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(QT_MIN_VERSION "6.6")
set(KF_MIN_VERSION "6.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Quick
    QuickControls2
    Widgets
    WaylandClient
)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    Config
    Crash
    DBusAddons
    WindowSystem
    Service
    Package
    IconThemes
    KIO
    XmlGui
    I18n
    ColorScheme
    Kirigami2
)

find_package(LayerShellQt REQUIRED)

find_path(PLASMA_WAYLAND_PROTOCOLS_DIR
    NAMES plasma-window-management.xml
    PATHS /usr/share/plasma-wayland-protocols
)
if(NOT PLASMA_WAYLAND_PROTOCOLS_DIR)
    message(FATAL_ERROR "plasma-wayland-protocols is required (plasma-window-management.xml not found)")
endif()

add_subdirectory(src)
add_subdirectory(data)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

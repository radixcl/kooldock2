cmake_minimum_required(VERSION 3.21)

project(kooldock2 VERSION 0.0.3 LANGUAGES CXX C)

set(PROJECT_BUG_ADDRESS "https://gitlab.com/radixcl/kooldock2/issues")

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
    DBus
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
)

find_package(KWayland REQUIRED)
# wlr-layer-shell, used only for the optional invisible "spacer" surface that
# reserves screen space (exclusive zone) so maximized windows don't sit behind
# the dock. The main dock window stays on org_kde_plasma_shell (Role::Panel) --
# that's what keeps it visible during "Show Desktop" -- but that protocol has
# no strut/exclusive-zone API, so the spacer uses layer-shell purely for its
# exclusive zone. The two protocols never decorate the same window.
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

# ── CPack / .deb packaging ──────────────────────────────────────
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Matias Fernandez <matias.fernandez@gmail.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "matias.fernandez@gmail.com")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qt6-base (>=6.6), qt6-declarative (>=6.6), qt6-wayland, libkf6config6, libkf6coreaddons6, libkf6windowsystem6, libkf6kio6, libkf6i18n6, libkwaylandclient6, liblayershellqtinterface6, qml6-module-qtquick-controls"
)
set(CPACK_DEBIAN_PACKAGE_SECTION "kde")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://gitlab.com/radixcl/kooldock2")
# Architecture: map CMake system processor to Debian arch names
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
else()
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()
# Architectures: amd64, arm64, etc.
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")
include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

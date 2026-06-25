add_executable(kooldock2
    main.cpp
    kooldock.cpp
    kooldock.h
    item.h
    dockmodel.cpp
    dockmodel.h
    launcheritems.cpp
    launcheritems.h
    windowtasks.cpp
    windowtasks.h
    waylandwindowtasks.cpp
    waylandwindowtasks.h
    windowactions.cpp
    windowactions.h
    unitylauncherwatcher.cpp
    unitylauncherwatcher.h
)

kconfig_add_kcfg_files(kooldock2 GENERATE_MOC kooldocksettings.kcfgc)

qt_generate_wayland_protocol_client_sources(kooldock2
    FILES ${PLASMA_WAYLAND_PROTOCOLS_DIR}/plasma-window-management.xml
)

set(_wayland_protocol_c ${CMAKE_CURRENT_BINARY_DIR}/wayland-plasma-window-management-protocol.c)
set_source_files_properties(${_wayland_protocol_c} PROPERTIES
    GENERATED TRUE
    LANGUAGE C
)
target_sources(kooldock2 PRIVATE ${_wayland_protocol_c})

target_sources(kooldock2 PRIVATE
    kooldock.qrc
)

target_compile_definitions(kooldock2 PRIVATE
    QT_USE_QSTRINGBUILDER
)

target_include_directories(kooldock2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Generate version header from CMake project() call in top-level CMakeLists.txt
configure_file(version.h.in version.h @ONLY)

target_link_libraries(kooldock2 PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::WaylandClient
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::Crash
    KF6::DBusAddons
    KF6::WindowSystem
    KF6::Service
    KF6::Package
    KF6::IconThemes
    KF6::KIOCore
    KF6::KIOWidgets
    KF6::XmlGui
    KF6::I18n
    KF6::ColorScheme
    Plasma::KWaylandClient
)

install(TARGETS kooldock2 ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

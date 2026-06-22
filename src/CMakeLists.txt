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
    windowactions.cpp
    windowactions.h
)

kconfig_add_kcfg_files(kooldock2 GENERATE_MOC kooldocksettings.kcfgc)

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

target_link_libraries(kooldock2 PRIVATE
    Qt6::Core
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
    KF6::Kirigami2
    LayerShellQtInterface
)

install(TARGETS kooldock2 ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

install(FILES kooldock.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})

configure_file(org.kde.kooldock2.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kooldock2.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kooldock2.desktop DESTINATION ${KDE_INSTALL_APPDIR})

include(ECMInstallIcons)
ecm_install_icons(ICONS icons/sc-apps-kooldock2.svg DESTINATION ${KDE_INSTALL_ICONDIR})

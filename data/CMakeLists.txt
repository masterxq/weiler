# SPDX-License-Identifier: GPL-2.0-or-later
# Program data and desktop integration

set(_appid io.github.masterxq.Weiler)

install(FILES fonts/Lato-Regular.ttf fonts/Lato-Bold.ttf fonts/NotoSerifDisplay-SemiBold.ttf
        DESTINATION ${WEILER_INSTALL_DATADIR}/fonts)
install(DIRECTORY music/ DESTINATION ${WEILER_INSTALL_DATADIR}/music FILES_MATCHING PATTERN "*.ogg")
install(FILES gamedata.list DESTINATION ${WEILER_INSTALL_DATADIR})
# RTTR: world descriptions, campaigns, sound conversion, player colors (the translation is installed compiled)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/rttr/data/RTTR/ DESTINATION ${WEILER_INSTALL_DATADIR}/RTTR
        PATTERN languages EXCLUDE)

install(PROGRAMS ${PROJECT_SOURCE_DIR}/tools/weiler-pack-data DESTINATION ${WEILER_INSTALL_BINDIR})
install(FILES man/weiler.6 man/weiler-pack-data.6 DESTINATION ${CMAKE_INSTALL_MANDIR}/man6)

set(WEILER_EXEC ${CMAKE_INSTALL_PREFIX}/${WEILER_INSTALL_BINDIR}/weiler)
configure_file(desktop/${_appid}.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${_appid}.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${_appid}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES desktop/${_appid}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES icons/${_appid}.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
foreach(_size 16 22 24 32 48 64 128 256 512)
    install(FILES icons/${_size}/${_appid}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps)
endforeach()

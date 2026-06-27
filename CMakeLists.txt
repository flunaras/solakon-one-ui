cmake_minimum_required(VERSION 3.20)

project(solakon-one-ui
    VERSION 1.0.0
    DESCRIPTION "FoxESS Solakon One Inverter Monitor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt6 components ─────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    SerialBus
    Charts
)

include(FeatureSummary)

# ── Source files ───────────────────────────────────────────────────────────────
set(SRCS
    src/main.cpp
    src/modbusapi.cpp
    src/mainwindow.cpp
    src/connectwindow.cpp
    src/dashboardwidget.cpp
    src/pvwidget.cpp
    src/gridwidget.cpp
    src/batterywidget.cpp
    src/energywidget.cpp
    src/infowidget.cpp
    src/alarmwidget.cpp
    src/settingswidget.cpp
    src/remotecontrolwidget.cpp
    src/pvchartwidget.cpp
)

set(HDRS
    src/inverterdata.h
    src/modbusapi.h
    src/mainwindow.h
    src/connectwindow.h
    src/dashboardwidget.h
    src/pvwidget.h
    src/gridwidget.h
    src/batterywidget.h
    src/energywidget.h
    src/infowidget.h
    src/alarmwidget.h
    src/settingswidget.h
    src/remotecontrolwidget.h
    src/pvchartwidget.h
    src/powerformat.h
)

add_executable(solakon-one-ui ${SRCS} ${HDRS})

target_link_libraries(solakon-one-ui
    Qt6::Core
    Qt6::Widgets
    Qt6::SerialBus
    Qt6::Charts
)

# ── Install ────────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS solakon-one-ui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES data/io.github.flunaras.solakon-one-ui.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# ── CPack packaging ────────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME        "solakon-one-ui")
set(CPACK_PACKAGE_VERSION     "${PROJECT_VERSION}")
set(CPACK_PACKAGE_RELEASE     "1")
set(CPACK_PACKAGE_VENDOR      "Ulf Saran")
set(CPACK_PACKAGE_CONTACT     "Ulf Saran <ulf.saran@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION
"solakon-one-ui monitors and controls the FoxESS Solakon One solar inverter
over the local network via Modbus TCP. No cloud account required.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(NOT CPACK_GENERATOR)
    set(CPACK_GENERATOR "RPM")
endif()

# ── RPM ────────────────────────────────────────────────────────────────────────
set(CPACK_RPM_PACKAGE_LICENSE       "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP         "Productivity/Scientific/Other")
set(CPACK_RPM_PACKAGE_URL           "https://github.com/flunaras/solakon-one-ui")
set(CPACK_RPM_PACKAGE_RELEASE       "${CPACK_PACKAGE_RELEASE}")
set(CPACK_RPM_PACKAGE_ARCHITECTURE  "${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RPM_PACKAGE_REQUIRES
    "libQt6Core6, libQt6SerialBus6, libQt6Charts6")

set(CPACK_RPM_SPEC_MORE_DEFINE "
%post
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
%postun
if [ $1 -eq 0 ] ; then
    /bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null
    /usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi
%posttrans
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
")

# ── DEB ────────────────────────────────────────────────────────────────────────
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ulf Saran <ulf.saran@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION    "science")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE   "https://github.com/flunaras/solakon-one-ui")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6t64, libqt6serialbus6, libqt6charts6, libqt6gui6, libqt6widgets6")

include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)

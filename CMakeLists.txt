cmake_minimum_required(VERSION 3.20)
project(LANMonitor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Sql)

# Use pkg-config to find libcrafter
find_package(PkgConfig REQUIRED)
pkg_check_modules(CRAFTER REQUIRED IMPORTED_TARGET crafter)

# Find pcap explicitly
find_library(PCAP_LIBRARY NAMES pcap REQUIRED)

# Add include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

add_executable(LANMonitor
    main.cpp
    core/NetworkManager.cpp
    core/PacketCapture.cpp
    core/TrafficMonitor.cpp
    core/FirewallManager.cpp
    core/DHCPManager.cpp
    core/DhcpServer.cpp
    core/DhcpServer.h
    core/CaptivePortalManager.cpp
    core/DnsResponder.cpp
    core/BlockHttpServer.cpp
    gui/StaticLeaseDialog.cpp
    gui/DeviceMonitorPage.cpp
    gui/DHCPPage.cpp
    core/IPCalculator.cpp
    gui/IPCalculatorPage.cpp
    gui/TrafficPage.cpp
    gui/StartupModePage.cpp
    gui/StartupModePage.h
    gui/AppSettings.cpp
    gui/ToggleSwitch.cpp
    gui/SettingsPage.cpp
    gui/PortScanDialog.cpp
    gui/PortScanDialog.h
    core/DatabaseManager.cpp
    core/DatabaseManager.h
    core/PortScanner.cpp
    core/PortScanner.h
    core/Types.h
    core/NetworkManager.h
    core/HostnameResolver.cpp
    core/HostnameResolver.h
    core/MacVendorManager.cpp
    core/MacVendorManager.h
    core/RouterDetector.h
    core/RouterDetector.cpp
    core/VulnerabilityScanner.h
    core/VulnerabilityScanner.cpp
    resources.qrc
    core/Device.h
    gui/MainWindow.cpp
    gui/MainWindow.h
    gui/DeviceTable.cpp
    gui/DeviceTable.h
    gui/DeviceTableModel.cpp
    gui/DeviceTableModel.h
    gui/DeviceTableDelegate.cpp
    gui/DeviceTableDelegate.h
    gui/TopologyWidget.cpp
    gui/TopologyWidget.h
    gui/RouterPage.h
    gui/RouterPage.cpp
    gui/BlockedDevicesPage.h
    gui/BlockedDevicesPage.cpp
    gui/VulnerabilityPage.h
    gui/VulnerabilityPage.cpp
    utils/Logger.h
)

target_link_libraries(LANMonitor PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Svg
    Qt6::Sql
    PkgConfig::CRAFTER
    ${PCAP_LIBRARY}
    pthread
)

# Copy user-editable router model DB next to the executable at build time
add_custom_command(TARGET LANMonitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:LANMonitor>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/data/router_models.json
        $<TARGET_FILE_DIR:LANMonitor>/data/router_models.json
    COMMENT "Copying router_models.json to build output"
)

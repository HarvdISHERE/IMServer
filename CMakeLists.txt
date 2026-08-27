cmake_minimum_required(VERSION 3.19)
project(IMServerConsole LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Sql)

qt_standard_project_setup()

qt_add_executable(IMServerConsole
    main.cpp
    imserver.h
    imserver.cpp
)

target_link_libraries(IMServerConsole
    PRIVATE
        Qt::Core
        Qt::Network
        Qt::Sql
)

install(TARGETS IMServerConsole
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

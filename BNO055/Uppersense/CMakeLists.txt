cmake_minimum_required(VERSION 3.16)

project(Uppersense VERSION 0.1 LANGUAGES CXX)

# UIC/MOC/RCC automáticos
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==== Encontrar Qt (5 o 6) ====
# Necesitamos Widgets y SerialPort
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialPort)

# ==== Lista de fuentes ====
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    headerwidget.cpp
    headerwidget.h
    mainwindow.ui
    resources.qrc
)

# ==== Crear ejecutable según versión de Qt ====
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Uppersense
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(Uppersense
        ${PROJECT_SOURCES}
    )
endif()

# ==== Enlazar librerías de Qt ====
target_link_libraries(Uppersense
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::SerialPort
)

# ==== Propiedades de bundle (Windows/macOS) ====
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Uppersense)
endif()

set_target_properties(Uppersense PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# ==== Instalación (opcional, no molesta) ====
include(GNUInstallDirs)
install(TARGETS Uppersense
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Uppersense)
endif()

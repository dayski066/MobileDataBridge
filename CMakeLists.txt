cmake_minimum_required(VERSION 3.21)
project(MobileDataBridge VERSION 0.1 LANGUAGES CXX)

# Configuración básica del proyecto
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Buscar Qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Svg)

# Fuentes del proyecto
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    devicemanager.cpp
    devicemanager.h
    dataanalyzer.cpp
    dataanalyzer.h
    datatransfermanager.cpp
    datatransfermanager.h
    transferstatisticsdialog.cpp
    transferstatisticsdialog.h
    transferstatisticsdialog.ui
    resources.qrc
    iconprovider.cpp
    iconprovider.h
    statemanager.cpp
    statemanager.h
)

# Crear el ejecutable
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(MobileDataBridge WIN32
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        iconprovider.h iconprovider.cpp
        statemanager.h statemanager.cpp
        adbsocketclient.h adbsocketclient.cpp
    )
else()
    if(ANDROID)
        add_library(MobileDataBridge SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(MobileDataBridge WIN32
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# Enlazar Qt
target_link_libraries(MobileDataBridge PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Svg
)

# Configuración específica para macOS
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.MobileDataBridge)
endif()
set_target_properties(MobileDataBridge PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# Copiar herramientas externas al directorio de compilación (para desarrollo)
function(copy_tools SOURCE_DIR DEST_DIR)
    if(EXISTS "${SOURCE_DIR}")
        file(GLOB_RECURSE TOOL_FILES LIST_DIRECTORIES false "${SOURCE_DIR}/*")
        foreach(FILE ${TOOL_FILES})
            # Obtener la ruta relativa del archivo dentro de SOURCE_DIR
            file(RELATIVE_PATH RELATIVE_FILE "${SOURCE_DIR}" "${FILE}")
            # Construir la ruta de destino
            set(DESTINATION_FILE "${DEST_DIR}/${RELATIVE_FILE}")
            # Crear el directorio de destino si no existe
            get_filename_component(DESTINATION_DIR "${DESTINATION_FILE}" DIRECTORY)
            file(MAKE_DIRECTORY "${DESTINATION_DIR}")
            # Copiar el archivo
            file(COPY "${FILE}" DESTINATION "${DESTINATION_DIR}")
        endforeach()
    else()
        message(WARNING "Directorio no encontrado: ${SOURCE_DIR}")
    endif()
endfunction()

# Copiar adb
if(WIN32)
    copy_tools("${CMAKE_SOURCE_DIR}/tools/adb" "${CMAKE_BINARY_DIR}/tools/adb")
else()
    copy_tools("${CMAKE_SOURCE_DIR}/tools/adb" "${CMAKE_BINARY_DIR}/tools/adb")
endif()

# Copiar libimobiledevice (x32 y x64)
copy_tools("${CMAKE_SOURCE_DIR}/tools/libimobiledevice/x32" "${CMAKE_BINARY_DIR}/tools/libimobiledevice/x32")
copy_tools("${CMAKE_SOURCE_DIR}/tools/libimobiledevice/x64" "${CMAKE_BINARY_DIR}/tools/libimobiledevice/x64")

# Instalar el ejecutable y las herramientas
include(GNUInstallDirs)
install(TARGETS MobileDataBridge
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Instalar las herramientas junto al ejecutable (para producción)
if(WIN32)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/adb")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/tools/adb/" DESTINATION "${CMAKE_INSTALL_BINDIR}/tools/adb")
    endif()
else()
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/adb")
        install(DIRECTORY "${CMAKE_SOURCE_DIR}/tools/adb/" DESTINATION "${CMAKE_INSTALL_BINDIR}/tools/adb")
    endif()
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/libimobiledevice/x32")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/tools/libimobiledevice/x32" DESTINATION "${CMAKE_INSTALL_BINDIR}/tools/libimobiledevice/x32")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/tools/libimobiledevice/x64")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/tools/libimobiledevice/x64" DESTINATION "${CMAKE_INSTALL_BINDIR}/tools/libimobiledevice/x64")
endif()

# Finalizar el ejecutable si se usa Qt6
if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(MobileDataBridge)
endif()

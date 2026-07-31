cmake_minimum_required(VERSION 3.16)
project(NanoPulse
    VERSION 0.0.6
    DESCRIPTION "Lightweight API Testing Desktop Application"
    LANGUAGES CXX
)

set(NANOPULSE_BUILD_VERSION "${PROJECT_VERSION}" CACHE STRING
    "Version embedded in the NanoPulse executable")
if(NOT NANOPULSE_BUILD_VERSION MATCHES
   "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "NANOPULSE_BUILD_VERSION must be a semantic version.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(NANOPULSE_STATIC_QT "Use a statically built Qt installation" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Sql Widgets)

qt_add_executable(NanoPulse
    src/main.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/request_method_colors.h src/ui/request_method_colors.cpp
    src/ui/request_panel.h src/ui/request_panel.cpp
    src/ui/response_viewer.h src/ui/response_viewer.cpp
    src/ui/text_search_bar.h src/ui/text_search_bar.cpp
    src/ui/collections_sidebar.h src/ui/collections_sidebar.cpp
    src/ui/environment_manager.h src/ui/environment_manager.cpp
    src/network/api_client.h src/network/api_client.cpp
    src/network/request.h
    src/storage/sqlite_manager.h src/storage/sqlite_manager.cpp
    src/storage/settings_manager.h src/storage/settings_manager.cpp
    src/import/swagger_importer.h src/import/swagger_importer.cpp
    src/import/postman_importer.h src/import/postman_importer.cpp
    src/import/postman_exporter.h src/import/postman_exporter.cpp
    src/import/curl_importer.h src/import/curl_importer.cpp
    src/import/http_file_importer.h src/import/http_file_importer.cpp
    src/import/har_importer.h src/import/har_importer.cpp
    src/import/external_collection_importer.h src/import/external_collection_importer.cpp
    src/import/bruno_importer.h src/import/bruno_importer.cpp
    src/utils/json_helper.h src/utils/json_helper.cpp
    src/utils/source_formatter.h src/utils/source_formatter.cpp
    src/utils/simple_yaml.h src/utils/simple_yaml.cpp
    src/utils/variable_substitution.h src/utils/variable_substitution.cpp
    src/resources/application.qrc
)

target_link_libraries(NanoPulse PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Widgets
)
target_include_directories(NanoPulse PRIVATE third_party)

target_compile_definitions(NanoPulse PRIVATE
    NANOPULSE_VERSION=\"${NANOPULSE_BUILD_VERSION}\"
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(MSVC)
    target_compile_options(NanoPulse PRIVATE $<$<CONFIG:Release>:/O2 /MP>)
    target_link_options(NanoPulse PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
else()
    target_compile_options(NanoPulse PRIVATE $<$<CONFIG:Release>:-O3>)
    if(CMAKE_STRIP)
        add_custom_command(TARGET NanoPulse POST_BUILD
            COMMAND $<$<CONFIG:Release>:${CMAKE_STRIP}>
                    $<$<CONFIG:Release>:$<TARGET_FILE:NanoPulse>>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()

if(NANOPULSE_STATIC_QT AND NOT QT_FEATURE_static)
    message(FATAL_ERROR "NANOPULSE_STATIC_QT requires a statically built Qt installation.")
endif()

install(TARGETS NanoPulse
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

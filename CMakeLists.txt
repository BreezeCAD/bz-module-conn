# BreezCONN — модуль BreezeCAD «Продолжить на устройстве» (ADR-0011):
# библиотека bz-conn и module.json, упакованные в .bzmod.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure
#
# Пакет: build/packages/bz-conn-<version>-<system>-<arch>.bzmod — его ставят
# в BreezeCAD через «Настройки → Модули → Установить модуль…».
# id, version и имя библиотеки берутся из module.json — правьте только его.
cmake_minimum_required(VERSION 3.21)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/module.json BZ_MANIFEST)
string(JSON BZ_ID      GET "${BZ_MANIFEST}" id)
string(JSON BZ_VERSION GET "${BZ_MANIFEST}" version)
string(JSON BZ_LIBRARY GET "${BZ_MANIFEST}" library)

project(bz_module_${BZ_ID} VERSION ${BZ_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
# Модуль лежит не рядом с программой: среда выполнения C/C++ — внутри библиотеки.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)

if(MSVC)   # те же предупреждения, что у BreezeCAD (cmake/CompilerWarnings.cmake)
    set(BZ_WARNINGS /W4 /permissive- /utf-8)
else()
    set(BZ_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
                    -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference -Wno-unused-parameter)
endif()

# --- Сторонние библиотеки (third_party/README.md) ------------------------------
add_library(bz_json INTERFACE)
target_include_directories(bz_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann)
add_library(bz_monocypher STATIC third_party/monocypher/monocypher.c)
target_include_directories(bz_monocypher SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher)
add_library(bz_qrcodegen STATIC third_party/qrcodegen/qrcodegen.cpp)
target_include_directories(bz_qrcodegen SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/qrcodegen)

# --- Работа модуля без таблицы программы (её же гоняют тесты) --------------------
add_library(bz_conn_core STATIC src/Seal.cpp src/Net.cpp src/Conn.cpp)
target_include_directories(bz_conn_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
target_link_libraries(bz_conn_core PUBLIC bz_json bz_monocypher bz_qrcodegen Threads::Threads)
target_compile_options(bz_conn_core PRIVATE ${BZ_WARNINGS})
if(WIN32)
    target_link_libraries(bz_conn_core PUBLIC ws2_32)
endif()

# --- Библиотека модуля -----------------------------------------------------------
set(BZ_STAGE ${PROJECT_BINARY_DIR}/stage/${BZ_ID})
add_library(module SHARED src/ConnModule.cpp)
target_link_libraries(module PRIVATE bz_conn_core)
target_compile_options(module PRIVATE ${BZ_WARNINGS})
set_target_properties(module PROPERTIES
    OUTPUT_NAME ${BZ_LIBRARY}
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY ${BZ_STAGE}
    RUNTIME_OUTPUT_DIRECTORY ${BZ_STAGE}
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)   # import-библиотека Windows — не в пакет
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)       # Visual Studio: без подпапок Release/
    string(TOUPPER ${cfg} CFG)
    set_target_properties(module PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${CFG} ${BZ_STAGE} RUNTIME_OUTPUT_DIRECTORY_${CFG} ${BZ_STAGE})
endforeach()
if(WIN32)
    set_target_properties(module PROPERTIES PREFIX "")   # <library>.dll — так ищет BreezeCAD
    if(MINGW)
        target_link_options(module PRIVATE -static)
        target_sources(module PRIVATE src/MingwCompat.c)   # см. комментарий в файле
    endif()
endif()
if(APPLE)
    set_target_properties(module PROPERTIES INSTALL_NAME_DIR "@loader_path")
endif()

# --- Пакет .bzmod ------------------------------------------------------------
if(WIN32)
    set(BZ_SYSTEM windows)
elseif(APPLE)
    set(BZ_SYSTEM macos)
else()
    set(BZ_SYSTEM linux)
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" BZ_ARCH)
if(APPLE AND CMAKE_OSX_ARCHITECTURES)   # флаг macOS на других системах ничего не значит
    set(BZ_ARCH ${CMAKE_OSX_ARCHITECTURES})
endif()
if(BZ_ARCH MATCHES "^(amd64|x86_64|x64)$")
    set(BZ_ARCH x64)
elseif(BZ_ARCH MATCHES "^(arm64|aarch64)$")
    set(BZ_ARCH arm64)
endif()

set(BZ_PACKAGE ${PROJECT_BINARY_DIR}/packages/bz-${BZ_ID}-${BZ_VERSION}-${BZ_SYSTEM}-${BZ_ARCH}.bzmod)
add_custom_command(OUTPUT ${BZ_PACKAGE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/packages
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/module.json ${BZ_STAGE}/module.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/README.md ${BZ_STAGE}/README.md
    COMMAND ${CMAKE_COMMAND} -E rm -f ${BZ_PACKAGE}
    COMMAND ${CMAKE_COMMAND} -E chdir ${PROJECT_BINARY_DIR}/stage ${CMAKE_COMMAND} -E tar cf ${BZ_PACKAGE} --format=zip ${BZ_ID}
    DEPENDS module ${CMAKE_CURRENT_SOURCE_DIR}/module.json ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    COMMENT "Пакет модуля: ${BZ_PACKAGE}"
    VERBATIM)
add_custom_target(package_bzmod ALL DEPENDS ${BZ_PACKAGE})

# --- Проверка без BreezeCAD ---------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(smoke tests/smoke.c)   # загрузка, как в программе; старая таблица без окон
    target_include_directories(smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
    target_link_libraries(smoke PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(smoke module)
    add_test(NAME module_loads COMMAND smoke $<TARGET_FILE:module>)

    add_executable(bz_conn_tests tests/main.cpp tests/ConnTests.cpp)
    target_include_directories(bz_conn_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest)
    target_link_libraries(bz_conn_tests PRIVATE bz_conn_core ${CMAKE_DL_LIBS})
    add_dependencies(bz_conn_tests module)
    target_compile_definitions(bz_conn_tests PRIVATE BZ_CONN_MODULE="$<TARGET_FILE:module>")
    add_test(NAME conn COMMAND bz_conn_tests)
endif()

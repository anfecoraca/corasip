cmake_minimum_required(VERSION 3.16)
project(corasip VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings are errors.
add_compile_options(-Wall -Wextra -Werror)

# PJSIP is the pinned static build from scripts/build-pjsip.sh (M1 step 3).
set(CORASIP_PJ_PREFIX "${CMAKE_SOURCE_DIR}/third_party/pjproject-install" CACHE PATH "Static PJSIP install prefix")
set(ENV{PKG_CONFIG_PATH} "${CORASIP_PJ_PREFIX}/lib/pkgconfig")
execute_process(
    COMMAND pkg-config --static --libs libpjproject
    OUTPUT_VARIABLE PJ_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE PJ_RESULT
)
if(NOT PJ_RESULT EQUAL 0)
    message(FATAL_ERROR "libpjproject not found under ${CORASIP_PJ_PREFIX}. Run scripts/build-pjsip.sh first.")
endif()
execute_process(
    COMMAND pkg-config --cflags libpjproject
    OUTPUT_VARIABLE PJ_CFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
separate_arguments(PJ_LIBS_LIST UNIX_COMMAND "${PJ_LIBS}")
# PJSIP needs its -D defines. Its headers go on a system include path so their warnings do not hit -Werror.
string(REGEX MATCHALL "-D[^ ]+" PJ_DEF_FLAGS "${PJ_CFLAGS}")
set(PJ_DEFINES "")
foreach(flag ${PJ_DEF_FLAGS})
    string(SUBSTRING "${flag}" 2 -1 def)
    list(APPEND PJ_DEFINES "${def}")
endforeach()

link_directories(${CORASIP_PJ_PREFIX}/lib)

# Engine wrapper: the only code that includes PJSUA2 (M1 step 5).
add_library(corasip_engine STATIC src/engine/engine.cpp)
target_include_directories(corasip_engine PUBLIC src)
target_include_directories(corasip_engine SYSTEM PRIVATE ${CORASIP_PJ_PREFIX}/include)
target_compile_definitions(corasip_engine PRIVATE ${PJ_DEFINES})
target_compile_options(corasip_engine PRIVATE -ffile-prefix-map=${CMAKE_SOURCE_DIR}=corasip)
target_link_libraries(corasip_engine PRIVATE ${PJ_LIBS_LIST})

add_executable(corasip-cli src/cli/main.cpp)
target_compile_definitions(corasip-cli PRIVATE CORASIP_VERSION="${PROJECT_VERSION}" ${PJ_DEFINES})
target_include_directories(corasip-cli SYSTEM PRIVATE ${CORASIP_PJ_PREFIX}/include)
# Reproducible builds: strip build paths from debug info.
target_compile_options(corasip-cli PRIVATE -ffile-prefix-map=${CMAKE_SOURCE_DIR}=corasip)
target_link_libraries(corasip-cli PRIVATE corasip_engine ${PJ_LIBS_LIST})

add_executable(engine_smoke tests/engine_smoke.cpp)
target_compile_definitions(engine_smoke PRIVATE ${PJ_DEFINES})
target_include_directories(engine_smoke SYSTEM PRIVATE ${CORASIP_PJ_PREFIX}/include)
target_link_libraries(engine_smoke PRIVATE corasip_engine ${PJ_LIBS_LIST})

add_executable(engine_transport tests/engine_transport.cpp)
target_compile_definitions(engine_transport PRIVATE ${PJ_DEFINES})
target_include_directories(engine_transport SYSTEM PRIVATE ${CORASIP_PJ_PREFIX}/include)
target_link_libraries(engine_transport PRIVATE corasip_engine ${PJ_LIBS_LIST})

add_executable(engine_call tests/engine_call.cpp)
target_compile_definitions(engine_call PRIVATE ${PJ_DEFINES})
target_include_directories(engine_call SYSTEM PRIVATE ${CORASIP_PJ_PREFIX}/include)
target_link_libraries(engine_call PRIVATE corasip_engine ${PJ_LIBS_LIST})

# Core logic for M2 (no Qt): settings file and account rules.
add_library(corasip_core STATIC src/settings/settings.cpp src/app/account_validation.cpp src/storage/history.cpp src/storage/contacts.cpp src/storage/contacts_csv.cpp)
target_include_directories(corasip_core PUBLIC src)
target_link_libraries(corasip_core PUBLIC corasip_engine sqlite3)

add_executable(core_tests tests/unit/core_tests.cpp)
target_link_libraries(core_tests PRIVATE corasip_core)

add_executable(history_tests tests/unit/history_tests.cpp)
target_link_libraries(history_tests PRIVATE corasip_core)

add_executable(contacts_tests tests/unit/contacts_tests.cpp)
target_link_libraries(contacts_tests PRIVATE corasip_core)

# M2 application: Qt 6 GUI (Widgets). Qt is LGPL-3.0, dynamically linked (SPEC D3).
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
qt_add_resources(corasip_resources src/ui/resources.qrc)
add_executable(corasip src/ui/main.cpp src/ui/main_window.cpp src/ui/hotkey_x11.cpp src/ui/ladder_view.cpp src/ui/account_dialog.cpp src/ui/phone_engine.cpp ${corasip_resources})
find_package(X11 REQUIRED)
target_link_libraries(corasip PRIVATE corasip_core Qt6::Widgets X11::X11)

qt_add_resources(gui_resources src/ui/resources.qrc)
add_executable(gui_smoke tests/unit/gui_smoke.cpp src/ui/main_window.cpp src/ui/hotkey_x11.cpp src/ui/ladder_view.cpp src/ui/account_dialog.cpp src/ui/phone_engine.cpp ${gui_resources})
target_link_libraries(gui_smoke PRIVATE corasip_core Qt6::Widgets X11::X11)
add_test(NAME gui_smoke COMMAND gui_smoke)

qt_add_resources(calllist_resources src/ui/resources.qrc)
add_executable(call_list_test tests/unit/call_list_test.cpp src/ui/main_window.cpp src/ui/hotkey_x11.cpp src/ui/ladder_view.cpp src/ui/account_dialog.cpp src/ui/phone_engine.cpp ${calllist_resources})
target_link_libraries(call_list_test PRIVATE corasip_core Qt6::Widgets X11::X11)
add_test(NAME call_list COMMAND call_list_test)

enable_testing()
add_test(NAME core_unit COMMAND core_tests)
add_test(NAME history_unit COMMAND history_tests)
add_test(NAME contacts_unit COMMAND contacts_tests)
add_test(NAME engine_smoke COMMAND engine_smoke)
add_test(NAME engine_transport COMMAND sh ${CMAKE_SOURCE_DIR}/tests/transport/run.sh ${CMAKE_BINARY_DIR})
add_test(NAME engine_call COMMAND sh ${CMAKE_SOURCE_DIR}/tests/call/run.sh ${CMAKE_BINARY_DIR})
# Lab tests need the lab PBX and tests/.local credentials. Run with: ctest -R lab|audio
add_test(NAME engine_codec COMMAND sh ${CMAKE_SOURCE_DIR}/tests/codec/run.sh ${CMAKE_BINARY_DIR})
add_test(NAME engine_calling COMMAND sh ${CMAKE_SOURCE_DIR}/tests/calling/run.sh ${CMAKE_BINARY_DIR})
add_executable(transfer_call tests/transfer/transfer_call.cpp)
target_compile_definitions(transfer_call PRIVATE ${PJ_DEFINES})
target_include_directories(transfer_call SYSTEM PRIVATE ${CORASIP_PJ_PREFIX}/include)
target_link_libraries(transfer_call PRIVATE corasip_engine ${PJ_LIBS_LIST})
add_test(NAME engine_transfer COMMAND sh ${CMAKE_SOURCE_DIR}/tests/transfer/run.sh ${CMAKE_BINARY_DIR})
add_test(NAME lab_pbx COMMAND sh ${CMAKE_SOURCE_DIR}/tests/lab/run.sh ${CMAKE_BINARY_DIR})
add_test(NAME audio_path COMMAND sh ${CMAKE_SOURCE_DIR}/tests/audio/run.sh ${CMAKE_BINARY_DIR})

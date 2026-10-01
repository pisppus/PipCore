cmake_minimum_required(VERSION 3.16)
project(PipCoreSimulator LANGUAGES C CXX)

set(SIM_INI "${CMAKE_CURRENT_LIST_DIR}/Build/simulator.ini")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SIM_INI}")

set(_ini_root "")
set(_ini_kernel "")
set(_ini_app "")
if(EXISTS "${SIM_INI}")
    file(STRINGS "${SIM_INI}" _ini_lines)
    foreach(_line IN LISTS _ini_lines)
        string(STRIP "${_line}" _line)
        if(_line MATCHES "^project_root[ ]*=(.*)$")
            string(STRIP "${CMAKE_MATCH_1}" _ini_root)
        elseif(_line MATCHES "^kernel_dir[ ]*=(.*)$")
            string(STRIP "${CMAKE_MATCH_1}" _ini_kernel)
        elseif(_line MATCHES "^app_dir[ ]*=(.*)$")
            string(STRIP "${CMAKE_MATCH_1}" _ini_app)
        endif()
    endforeach()
    unset(_ini_lines)
endif()

set(PIPSIM_PROJECT_ROOT "" CACHE STRING "Override: PipCore project root (contains the PipCore component folder)")
set(PIPSIM_KERNEL_DIR "" CACHE STRING "Override: PipCore kernel component folder")
set(PIPSIM_APP_DIR "" CACHE STRING "Override: firmware app sources folder")
set(PIPSIM_PROJECT_NAME "" CACHE STRING "Window title prefix (defaults to the project root folder name)")
set(PIPSIM_ROOT "${_ini_root}")
if(PIPSIM_PROJECT_ROOT)
    set(PIPSIM_ROOT "${PIPSIM_PROJECT_ROOT}")
endif()

if(NOT PIPSIM_KERNEL_DIR)
    if(PIPSIM_ROOT)
        set(PIPSIM_KERNEL_DIR "${PIPSIM_ROOT}/PipCore")
    else()
        set(PIPSIM_KERNEL_DIR "${CMAKE_CURRENT_LIST_DIR}/../PipCore")
    endif()
endif()
if(NOT EXISTS "${PIPSIM_KERNEL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "PipCore kernel not found: ${PIPSIM_KERNEL_DIR}\n"
        "Run Simulator/Sim.ps1 once to pick the project folder, or set\n"
        "project_root / kernel_dir in Simulator/Build/simulator.ini")
endif()
set(PIPCORE_DIR "${PIPSIM_KERNEL_DIR}")

function(_sim_dir_has_sources dir out)
    set(${out} FALSE PARENT_SCOPE)
    if(NOT EXISTS "${dir}")
        return()
    endif()
    file(GLOB_RECURSE _found CONFIGURE_DEPENDS
        "${dir}/*.cpp" "${dir}/*.cc" "${dir}/*.cxx" "${dir}/*.c")
    list(LENGTH _found _count)
    if(_count GREATER 0)
        set(${out} TRUE PARENT_SCOPE)
    endif()
endfunction()

set(PIPSIM_APP "${_ini_app}")
if(PIPSIM_APP_DIR)
    set(PIPSIM_APP "${PIPSIM_APP_DIR}")
endif()
if(NOT PIPSIM_APP AND PIPSIM_ROOT)
    foreach(_candidate "${PIPSIM_ROOT}/src" "${PIPSIM_ROOT}/main")
        _sim_dir_has_sources("${_candidate}" _has)
        if(_has)
            set(PIPSIM_APP "${_candidate}")
            break()
        endif()
    endforeach()
    unset(_candidate)
endif()
unset(_has)

set(SIM_APP_SOURCES "")
if(PIPSIM_APP)
    if(NOT EXISTS "${PIPSIM_APP}")
        message(WARNING "Configured app folder does not exist: ${PIPSIM_APP} - starting with the setup stub")
        set(PIPSIM_APP "")
    else()
        file(GLOB_RECURSE SIM_APP_SOURCES CONFIGURE_DEPENDS
            "${PIPSIM_APP}/*.cpp" "${PIPSIM_APP}/*.cc" "${PIPSIM_APP}/*.cxx" "${PIPSIM_APP}/*.c")
        list(LENGTH SIM_APP_SOURCES _app_count)
        if(_app_count EQUAL 0)
            set(PIPSIM_APP "")
        endif()
        unset(_app_count)
    endif()
endif()

option(PIPSIM_AUDIO "Enable the PAC audio mixer in the simulator" ON)
option(PIPSIM_TOUCH "Enable touch (mouse) injection" ON)
option(PIPSIM_GUI "Windows: build as a windowed app without console" ON)
option(SIM_DEBUG "Debug build naming (simulator-debug)" OFF)

set(SIM_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${PIPCORE_DIR}/Graphics/Sprite.cpp"
    "${PIPCORE_DIR}/Graphics/Raster.cpp"
    "${PIPCORE_DIR}/Core/Memory.cpp"
    "${PIPCORE_DIR}/Core/Log.cpp"
    "${PIPCORE_DIR}/Network/Wifi.cpp"
    "${PIPCORE_DIR}/Network/Ota.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Host/Display/Simulator.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Host/Platform.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Host/Runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Host/SimText.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Host/Audio/Audio.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Host/Storage/Storage.cpp"
)

if(SIM_APP_SOURCES)
    list(APPEND SIM_SOURCES ${SIM_APP_SOURCES})
else()
    list(APPEND SIM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/NoApp.cpp")
    set(PIPSIM_SETUP_MODE ON)
endif()

if(PIPSIM_AUDIO)
    list(APPEND SIM_SOURCES
        "${PIPCORE_DIR}/Audio/Audio.cpp"
        "${PIPCORE_DIR}/Audio/Pac.cpp"
    )
endif()

if(WIN32)
    list(APPEND SIM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/Host/Windows/WindowWin32.cpp")
    if(MSVC)
        list(APPEND SIM_SOURCES "${CMAKE_CURRENT_LIST_DIR}/Host/Windows/app.manifest")
    endif()
endif()

add_executable(simulator ${SIM_SOURCES})

target_include_directories(simulator PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${PIPCORE_DIR}"
    "${PIPCORE_DIR}/Public"
)
if(PIPSIM_ROOT)
    target_include_directories(simulator PRIVATE "${PIPSIM_ROOT}")
endif()
if(PIPSIM_APP)
    target_include_directories(simulator PRIVATE "${PIPSIM_APP}")
    if(EXISTS "${PIPSIM_APP}/include")
        target_include_directories(simulator PRIVATE "${PIPSIM_APP}/include")
    endif()
endif()

target_compile_definitions(simulator PRIVATE
    $<$<BOOL:${PIPSIM_AUDIO}>:PIPCORE_ENABLE_AUDIO=1>
    $<$<BOOL:${PIPSIM_TOUCH}>:PIPCORE_ENABLE_TOUCH=1>
    $<$<BOOL:${PIPSIM_GUI}>:PIPSIM_GUI=1>
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(NOT PIPSIM_PROJECT_NAME AND PIPSIM_ROOT)
    get_filename_component(PIPSIM_PROJECT_NAME "${PIPSIM_ROOT}" NAME)
endif()
if(PIPSIM_PROJECT_NAME)
    target_compile_definitions(simulator PRIVATE PIPSIM_PROJECT_NAME="${PIPSIM_PROJECT_NAME}")
endif()

if(WIN32)
    target_link_libraries(simulator PRIVATE user32 gdi32 dwmapi ole32 comctl32 uxtheme
        mfplat mfreadwrite mfuuid)
    if(MSVC)
        target_compile_options(simulator PRIVATE /Zc:__cplusplus /permissive- /W3)
    endif()
endif()

if(PIPSIM_AUDIO AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/miniaudio/miniaudio.h")
    target_compile_definitions(simulator PRIVATE PIPCORE_DESKTOP_AUDIO_MINIAUDIO=1)
    target_include_directories(simulator PRIVATE "${CMAKE_CURRENT_LIST_DIR}/miniaudio")
elseif(PIPSIM_AUDIO)
    message(STATUS "miniaudio.h not found in Simulator/miniaudio - audio output is headless (mixer still active).")
endif()

set_property(TARGET simulator PROPERTY CXX_STANDARD 23)
set_property(TARGET simulator PROPERTY CXX_STANDARD_REQUIRED ON)

set(SIM_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/Build")
if(SIM_DEBUG)
    set_target_properties(simulator PROPERTIES OUTPUT_NAME "simulator-debug")
else()
    set_target_properties(simulator PROPERTIES OUTPUT_NAME "simulator")
endif()
set_target_properties(simulator PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SIM_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${SIM_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${SIM_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${SIM_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${SIM_OUTPUT_DIR}"
)

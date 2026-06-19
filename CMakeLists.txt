cmake_minimum_required(VERSION 3.20)
project(multiplayer_game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SERVER "Build the headless authoritative server" ON)
option(BUILD_CLIENT "Build the raylib desktop client" ON)

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.4
)
FetchContent_MakeAvailable(ixwebsocket)

if(EMSCRIPTEN AND TARGET ixwebsocket)
    # IXUserAgent.cpp omits PLATFORM_NAME on __unix__ targets that are not BSD/Linux.
    target_compile_definitions(ixwebsocket PRIVATE PLATFORM_NAME=\"web\")
endif()

add_library(net_common STATIC
    common/protocol.cpp
    common/socket_utils.cpp
    common/grid_map.cpp
    common/pathfinding.cpp
    common/enemies.cpp
    common/combat.cpp
    common/entity_registry.cpp
    common/skills.cpp
)
target_include_directories(net_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(net_common PUBLIC nlohmann_json::nlohmann_json)

if(BUILD_SERVER)
    add_executable(game_server
        server/main.cpp
        server/game_server.cpp
        server/tcp_listener.cpp
        server/ws_listener.cpp
    )
    target_link_libraries(game_server PRIVATE net_common ixwebsocket::ixwebsocket)
    if(WIN32)
        target_link_libraries(game_server PRIVATE ws2_32)
    endif()
endif()

if(BUILD_CLIENT)
    set(RAYLIB_PATH "C:/raylib/raylib" CACHE PATH "Path to raylib source root")

    add_executable(game_client
        client/main.cpp
        client/audio_manager.cpp
        client/viewport.cpp
        client/world_view.cpp
        client/pointer_input.cpp
        client/connection_config.cpp
        client/game_client.cpp
        client/ws_client.cpp
    )
    if(NOT EMSCRIPTEN)
        target_sources(game_client PRIVATE client/tcp_client.cpp)
    endif()
    target_include_directories(game_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${RAYLIB_PATH}/src
    )

    if (EMSCRIPTEN)
        target_compile_definitions(game_client PRIVATE PLATFORM_WEB)
        if(DEFINED WS_HOST_DEFAULT AND NOT WS_HOST_DEFAULT STREQUAL "")
            target_compile_definitions(game_client PRIVATE WS_HOST_DEFAULT="${WS_HOST_DEFAULT}")
        endif()
        set(RAYLIB_WEB_LIB "${RAYLIB_PATH}/src/libraylib.web.a")
        if(NOT EXISTS "${RAYLIB_WEB_LIB}")
            message(FATAL_ERROR
                "Missing ${RAYLIB_WEB_LIB}. Build raylib for web first (tools/build_web.ps1).")
        endif()
        target_link_options(game_client PRIVATE
            -sUSE_GLFW=3
            -sALLOW_MEMORY_GROWTH=1
            -sASYNCIFY
            $<$<CONFIG:Debug>:-sASSERTIONS=1>
            "--preload-file=${CMAKE_SOURCE_DIR}/assets@/assets"
        )
        set_target_properties(game_client PROPERTIES SUFFIX ".js")
    else()
        target_compile_definitions(game_client PRIVATE PLATFORM_DESKTOP)
        if(DEFINED SERVER_HOST_DEFAULT AND NOT SERVER_HOST_DEFAULT STREQUAL "")
            target_compile_definitions(game_client PRIVATE SERVER_HOST_DEFAULT="${SERVER_HOST_DEFAULT}")
        endif()
        if(DEFINED SERVER_PORT_DEFAULT AND NOT SERVER_PORT_DEFAULT STREQUAL "")
            target_compile_definitions(game_client PRIVATE SERVER_PORT_DEFAULT=${SERVER_PORT_DEFAULT})
        endif()
    endif()

    if(NOT EMSCRIPTEN)
        add_custom_command(TARGET game_client POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:game_client>/assets
            COMMENT "Copying assets next to game_client"
        )

        if(EXISTS "${CMAKE_SOURCE_DIR}/sfx")
            add_custom_command(TARGET game_client POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        ${CMAKE_SOURCE_DIR}/sfx $<TARGET_FILE_DIR:game_client>/sfx
                COMMENT "Copying sfx next to game_client"
            )
        endif()
    endif()

    if(WIN32)
        target_link_libraries(game_client PRIVATE
            net_common
            ixwebsocket::ixwebsocket
            ws2_32
            ${RAYLIB_PATH}/src/libraylib.a
            opengl32
            gdi32
            winmm
        )
    elseif(EMSCRIPTEN)
        target_link_libraries(game_client PRIVATE
            net_common
            ixwebsocket::ixwebsocket
            ${RAYLIB_WEB_LIB}
        )
    elseif(APPLE)
        find_library(COCOA_FRAMEWORK Cocoa)
        find_library(IOKIT_FRAMEWORK IOKit)
        find_library(OPENGL_FRAMEWORK OpenGL)
        target_link_libraries(game_client PRIVATE
            net_common
            ixwebsocket::ixwebsocket
            ${RAYLIB_PATH}/src/libraylib.a
            ${COCOA_FRAMEWORK}
            ${IOKIT_FRAMEWORK}
            ${OPENGL_FRAMEWORK}
        )
    else()
        target_link_libraries(game_client PRIVATE
            net_common
            ixwebsocket::ixwebsocket
            ${RAYLIB_PATH}/src/libraylib.a
            GL
            m
            pthread
            dl
            rt
            X11
        )
    endif()
endif()

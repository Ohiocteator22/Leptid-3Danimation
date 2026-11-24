# Leptid — CMake build (alternative to the Makefile; recommended for MSVC/Windows)
cmake_minimum_required(VERSION 3.12)
project(leptid CXX)

if(POLICY CMP0091) # MSVC runtime library selection
    cmake_policy(SET CMP0091 NEW)
endif()
# static CRT -> the Windows exe carries no vcruntime DLL dependency
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1) generate the embedded font sources (bin2cpp step, same as the Makefile)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)
add_executable(bin2cpp tools/bin2cpp.cpp)
set(FONTCPP ${CMAKE_BINARY_DIR}/generated/fontdata.cpp)
add_custom_command(
    OUTPUT ${FONTCPP}
    COMMAND bin2cpp ${CMAKE_SOURCE_DIR}/third_party/fonts/DejaVuSans.ttf DejaVuSans_font > ${FONTCPP}
    COMMAND bin2cpp ${CMAKE_SOURCE_DIR}/third_party/fonts/DejaVuSans-Bold.ttf DejaVuSansBold_font >> ${FONTCPP}
    DEPENDS bin2cpp third_party/fonts/DejaVuSans.ttf third_party/fonts/DejaVuSans-Bold.ttf
    COMMENT "Embedding fonts")

# brush tip + icon assets embedded the same way (single-file app)
set(ASSETCPP ${CMAKE_BINARY_DIR}/generated/assets.cpp)
add_custom_command(
    OUTPUT ${ASSETCPP}
    COMMAND ${CMAKE_COMMAND}
            -DBIN2CPP=$<TARGET_FILE:bin2cpp>
            -DOUT=${ASSETCPP}
            -DSRC_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_assets.cmake
    DEPENDS bin2cpp
    COMMENT "Embedding brush tips and icons")

file(GLOB_RECURSE APP_SRCS CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM APP_SRCS ${CMAKE_SOURCE_DIR}/src/app/main.cpp)

# Build-once guarantee: a custom target builds the generated sources exactly
# once; both executables list them as sources (to compile/link) AND depend on
# the custom target (to order the build). Without the custom target a parallel
# build can run the embed custom command twice — file(APPEND) then duplicates
# every symbol and the link fails.
add_custom_target(generated_sources ALL DEPENDS ${FONTCPP} ${ASSETCPP})

add_executable(leptid ${APP_SRCS} src/app/main.cpp ${FONTCPP} ${ASSETCPP})
add_dependencies(leptid generated_sources)
if (WIN32)
    target_sources(leptid PRIVATE packaging/leptid.rc)
endif()
target_include_directories(leptid PRIVATE src third_party/stb)

if (WIN32)
    # Win32 backend needs gdi32/user32 (linked by default in MSVC; explicit for
    # MinGW) and ws2_32 for the Ollama client's Winsock2 shim
    target_link_libraries(leptid PRIVATE user32 gdi32 ws2_32)
else()
    find_package(X11)
    if (X11_FOUND)
        target_compile_definitions(leptid PRIVATE LEPTID_HAS_X11)
        target_include_directories(leptid PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(leptid PRIVATE ${X11_LIBRARIES})
    endif()
endif()

file(GLOB_RECURSE TEST_SRCS CONFIGURE_DEPENDS tests/*.cpp)
add_executable(leptid_tests ${APP_SRCS} ${TEST_SRCS} ${FONTCPP} ${ASSETCPP})
add_dependencies(leptid_tests generated_sources)
target_include_directories(leptid_tests PRIVATE src third_party/stb)
if (WIN32)
    target_link_libraries(leptid_tests PRIVATE user32 gdi32 ws2_32)
elseif (X11_FOUND)
    target_compile_definitions(leptid_tests PRIVATE LEPTID_HAS_X11)
    target_include_directories(leptid_tests PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(leptid_tests PRIVATE ${X11_LIBRARIES})
endif()

enable_testing()
add_test(NAME leptid_tests COMMAND leptid_tests)

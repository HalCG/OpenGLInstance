cmake_minimum_required(VERSION 3.10)

project(OpenGL_OIT_Depth_Peeling)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PROJECT_MODULES_DIR "${CMAKE_SOURCE_DIR}/modules")

# GLFW (modules/cmake/FindGLFW3.cmake)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_MODULES_DIR}/cmake")
find_package(GLFW3 REQUIRED)

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# Assimp：优先 modules/assimp_x64-windows（相对路径），其次 VCPKG_ROOT（若已设置）
# 请将 vcpkg 安装产物中的 lib/*.lib 与 bin/*.dll 拷贝到对应目录，例如：
#   modules/assimp_x64-windows/lib/assimp-vc143-mt.lib
#   modules/assimp_x64-windows/bin/assimp-vc143-mt.dll
# ---------------------------------------------------------------------------
set(ASSIMP_ROOT "${PROJECT_MODULES_DIR}/assimp_x64-windows")
set(_assimp_from_modules FALSE)

if(EXISTS "${ASSIMP_ROOT}/include/assimp/Importer.hpp")
    find_library(ASSIMP_LIBRARY_RELEASE
        NAMES assimp-vc143-mt assimp-vc142-mt assimp
        PATHS "${ASSIMP_ROOT}/lib"
        NO_DEFAULT_PATH
    )
    find_library(ASSIMP_LIBRARY_DEBUG
        NAMES assimp-vc143-mtd assimp-vc142-mtd assimp
        PATHS "${ASSIMP_ROOT}/debug/lib"
        NO_DEFAULT_PATH
    )

    if(ASSIMP_LIBRARY_RELEASE OR ASSIMP_LIBRARY_DEBUG)
        find_file(ASSIMP_DLL_RELEASE
            NAMES assimp-vc143-mt.dll assimp-vc142-mt.dll assimp.dll
            PATHS "${ASSIMP_ROOT}/bin"
            NO_DEFAULT_PATH
        )
        find_file(ASSIMP_DLL_DEBUG
            NAMES assimp-vc143-mtd.dll assimp-vc142-mtd.dll
            PATHS "${ASSIMP_ROOT}/debug/bin"
            NO_DEFAULT_PATH
        )

        add_library(assimp::assimp SHARED IMPORTED GLOBAL)
        set_target_properties(assimp::assimp PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_ROOT}/include"
        )
        if(ASSIMP_LIBRARY_RELEASE)
            set_target_properties(assimp::assimp PROPERTIES
                IMPORTED_IMPLIB_RELEASE "${ASSIMP_LIBRARY_RELEASE}"
                IMPORTED_LOCATION_RELEASE "${ASSIMP_DLL_RELEASE}"
            )
        endif()
        if(ASSIMP_LIBRARY_DEBUG)
            set_target_properties(assimp::assimp PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${ASSIMP_LIBRARY_DEBUG}"
                IMPORTED_LOCATION_DEBUG "${ASSIMP_DLL_DEBUG}"
            )
        endif()
        set(_assimp_from_modules TRUE)
    endif()
endif()

if(NOT _assimp_from_modules)
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
        find_package(assimp CONFIG REQUIRED)
    else()
        message(FATAL_ERROR
            "Assimp not found under ${ASSIMP_ROOT}/lib (and debug/lib).\n"
            "Either copy assimp .lib/.dll into modules/assimp_x64-windows, "
            "or set VCPKG_ROOT and install assimp via vcpkg."
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Polyclipping：assimp 依赖项，同样优先 modules 相对路径
# 例如：modules/polyclipping_x64-windows/lib/polyclipping.lib
# ---------------------------------------------------------------------------
set(POLYCLIPPING_ROOT "${PROJECT_MODULES_DIR}/polyclipping_x64-windows")
set(_polyclipping_from_modules FALSE)

find_library(POLYCLIPPING_LIBRARY_RELEASE
    NAMES polyclipping
    PATHS "${POLYCLIPPING_ROOT}/lib"
    NO_DEFAULT_PATH
)
find_library(POLYCLIPPING_LIBRARY_DEBUG
    NAMES polyclipping
    PATHS "${POLYCLIPPING_ROOT}/debug/lib"
    NO_DEFAULT_PATH
)

if(POLYCLIPPING_LIBRARY_RELEASE OR POLYCLIPPING_LIBRARY_DEBUG)
    add_library(polyclipping::polyclipping STATIC IMPORTED GLOBAL)
    set_target_properties(polyclipping::polyclipping PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${POLYCLIPPING_ROOT}/include"
    )
    if(POLYCLIPPING_LIBRARY_RELEASE)
        set_target_properties(polyclipping::polyclipping PROPERTIES
            IMPORTED_LOCATION_RELEASE "${POLYCLIPPING_LIBRARY_RELEASE}"
        )
    endif()
    if(POLYCLIPPING_LIBRARY_DEBUG)
        set_target_properties(polyclipping::polyclipping PROPERTIES
            IMPORTED_LOCATION_DEBUG "${POLYCLIPPING_LIBRARY_DEBUG}"
        )
    endif()
    set(_polyclipping_from_modules TRUE)
endif()

if(NOT _polyclipping_from_modules AND NOT TARGET polyclipping::polyclipping)
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
        find_package(polyclipping CONFIG REQUIRED)
    else()
        message(FATAL_ERROR
            "Polyclipping not found under ${POLYCLIPPING_ROOT}/lib (and debug/lib).\n"
            "Copy polyclipping.lib into modules/polyclipping_x64-windows, "
            "or set VCPKG_ROOT and install polyclipping via vcpkg."
        )
    endif()
endif()

# 第三方静态库
add_library(glad STATIC "${PROJECT_MODULES_DIR}/glad4.6/src/glad.c")
target_include_directories(glad PUBLIC "${PROJECT_MODULES_DIR}/glad4.6/include")

add_library(stb_image STATIC "${PROJECT_MODULES_DIR}/stb_image/stb_image_wrap.cpp")
target_include_directories(stb_image PUBLIC
    "${PROJECT_MODULES_DIR}/stb_image"
    "${CMAKE_SOURCE_DIR}/include"
)

add_executable(${PROJECT_NAME}
    main.cpp
    src/DepthPeelingApp.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    ${GLFW3_INCLUDE_DIR}
    "${ASSIMP_ROOT}/include"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GLFW3_LIBRARY}
    glad
    stb_image
    assimp::assimp
    polyclipping::polyclipping
    OpenGL::GL
)

# 将 shaders / models 复制到 exe 同目录，避免从 build/Debug 运行时找不到 ../resources
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
    COMMENT "Copy resources directory next to executable"
)

# ---------------------------------------------------------------------------
# 运行时 DLL 部署（Windows）
# - 静态库（polyclipping 等 .lib）：无需拷贝
# - assimp / poly2tri / minizip / pugixml / zlib 等为 DLL，需与 exe 同目录
# - 从 modules 各包 bin 或 debug/bin 拷贝（不依赖 CMake target 是否存在）
# ---------------------------------------------------------------------------
if(WIN32)
    set(_MODULE_DLL_ROOTS
        "${ASSIMP_ROOT}"
        "${PROJECT_MODULES_DIR}/pugixml_x64-windows"
        "${PROJECT_MODULES_DIR}/minizip_x64-windows"
        "${PROJECT_MODULES_DIR}/jhasse-poly2tri_x64-windows"
        "${PROJECT_MODULES_DIR}/zlib_x64-windows"
    )
    string(REPLACE ";" "\\;" _MODULE_DLL_ROOTS_ESC "${_MODULE_DLL_ROOTS}")

    # Visual Studio 多配置：Debug 用 debug/bin，Release 用 bin
    if(CMAKE_CONFIGURATION_TYPES)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=debug/bin
                -Doutdir=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                -P "${CMAKE_SOURCE_DIR}/cmake/CopyModuleDlls.cmake"
            COMMENT "Copy Debug runtime DLLs from modules"
            CONFIGURATIONS Debug
        )
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=bin
                -Doutdir=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                -P "${CMAKE_SOURCE_DIR}/cmake/CopyModuleDlls.cmake"
            COMMENT "Copy Release runtime DLLs from modules"
            CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        )
    else()
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_MODULE_BIN_SUBDIR "debug/bin")
        else()
            set(_MODULE_BIN_SUBDIR "bin")
        endif()
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=${_MODULE_BIN_SUBDIR}
                -Doutdir=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                -P "${CMAKE_SOURCE_DIR}/cmake/CopyModuleDlls.cmake"
            COMMENT "Copy runtime DLLs from modules"
        )
    endif()

    # 可选：VCPKG_ROOT 下额外 DLL（modules 未收录时）
    if(DEFINED ENV{VCPKG_ROOT})
        set(_VCPKG_BIN "$ENV{VCPKG_ROOT}/installed/x64-windows/bin")
        set(_VCPKG_DEBUG_BIN "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/bin")
        if(CMAKE_CONFIGURATION_TYPES)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -Droots="${_VCPKG_DEBUG_BIN}"
                    -Dbinsub=
                    -Doutdir=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                    -P "${CMAKE_SOURCE_DIR}/cmake/CopyModuleDlls.cmake"
                CONFIGURATIONS Debug
            )
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -Droots="${_VCPKG_BIN}"
                    -Dbinsub=
                    -Doutdir=$<TARGET_FILE_DIR:${PROJECT_NAME}>
                    -P "${CMAKE_SOURCE_DIR}/cmake/CopyModuleDlls.cmake"
                CONFIGURATIONS Release RelWithDebInfo MinSizeRel
            )
        endif()
    endif()
endif()

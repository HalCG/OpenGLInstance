project(OpenGL_Anti_Aliasing)

add_executable(${PROJECT_NAME}
    main.cpp
    stb_image.cpp
    src/AntiAliasingApp.cpp
    src/AATestScene.cpp
    src/SceneRenderer.cpp
    src/Framebuffer.cpp
    src/PostProcess.cpp
    src/TaaPass.cpp
    src/PerfStats.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
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

ogl_ws_deploy_runtime(${PROJECT_NAME} OpenGL_Anti_Aliasing)

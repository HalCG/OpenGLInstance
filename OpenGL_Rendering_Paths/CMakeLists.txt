project(OpenGL_Rendering_Paths)

add_executable(${PROJECT_NAME}
    main.cpp
    stb_image.cpp
    src/RenderingPathsApp.cpp
    src/Scene.cpp
    src/LightManager.cpp
    src/PerfStats.cpp
    src/ForwardRenderer.cpp
    src/DeferredRenderer.cpp
    src/ForwardPlusRenderer.cpp
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

ogl_ws_deploy_runtime(${PROJECT_NAME} OpenGL_Rendering_Paths)

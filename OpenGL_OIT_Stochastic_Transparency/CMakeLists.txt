project(OpenGL_OIT_Stochastic_Transparency)

add_executable(${PROJECT_NAME}
    main.cpp
    src/StochasticTransparencyApp.cpp
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
    OpenGL::GL
)

if(TARGET pugixml::pugixml)
    target_link_libraries(${PROJECT_NAME} PRIVATE pugixml::pugixml)
endif()

ogl_ws_deploy_runtime(${PROJECT_NAME} OpenGL_OIT_Stochastic_Transparency)

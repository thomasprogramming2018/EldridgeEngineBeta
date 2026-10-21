cmake_minimum_required(VERSION 3.16)
project(EngineLabs LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(OpenGL_GL_PREFERENCE GLVND)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)
find_package(Vulkan QUIET)
# The checked-in SPIR-V works without a shader compiler. Rebuild it when available.
find_program(GLSLANG_VALIDATOR glslangValidator)
if(GLSLANG_VALIDATOR)
    foreach(shader hardware denoise)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/${shader}.comp.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -V ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/${shader}.comp -o ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/${shader}.comp.spv
            DEPENDS assets/shaders/${shader}.comp)
    endforeach()
endif()
function(enable_hardware_ray_tracing target)
    if(Vulkan_FOUND)
        target_compile_definitions(${target} PRIVATE ENGINE_HAS_VULKAN)
        target_link_libraries(${target} PRIVATE Vulkan::Vulkan)
    endif()
endfunction()

add_executable(EngineLabs
    src/main.cpp
    src/modules/componentsBackend/TransformComponent/TransformComponent.cpp
    src/modules/componentsBackend/TransformComponent/TransformComponent.h
    src/modules/scene/SceneObject.cpp
    src/modules/componentsBackend/LightComponent/LightComponent.cpp
    src/modules/scene/Scene.cpp
    src/modules/scene/SceneSerialization.cpp
    src/modules/scene/SceneManager.cpp
    src/modules/componentsBackend/TerrainComponent/TerrainComponent.cpp
    src/modules/componentsBackend/MeshComponent/MeshComponent.cpp
    src/modules/assets/MeshImporter.cpp
    src/modules/editor/TransformGizmo.cpp
    src/modules/renderer/Renderer.cpp
    src/modules/renderer/Renderer.h
    src/modules/renderer/GPUMesh.cpp
    src/modules/renderer/PostProcessor.cpp
    src/modules/renderer/HybridLighting.cpp
    src/modules/editor/LightingDemo.cpp
    src/modules/renderer/ShadowMap.cpp
    src/modules/renderer/SoftwareRayTracer.cpp
    src/modules/renderer/VulkanRayTracer.cpp
    src/modules/renderer/HardwareViewport.cpp
    src/modules/project/ProjectFile.cpp
    src/modules/editor/ObjectCreationDialog.cpp
    src/modules/editor/RayTraceDialog.cpp
    src/modules/editor/TerrainWidget.cpp
    src/modules/editor/LightWidget.cpp
    src/modules/shader/Shader.cpp
    src/modules/shader/ShaderAsset.cpp
    src/modules/editor/ShaderEditorDialog.cpp
    src/modules/resources/ResourceManager.cpp
    src/modules/material/Material.cpp
    src/modules/editor/MaterialEditor.h
    src/modules/editor/MaterialEditor.cpp
    assets/shaders/shaders.qrc
    src/modules/editor/InspectorWidget.cpp
    src/modules/editor/MeshComponentWidget.h
    src/modules/editor/MeshComponentWidget.cpp
    src/modules/editor/HierarchyWidget.cpp
    src/modules/object_factory/ObjectFactory.cpp
    src/modules/editor/HierarchyTree.h
    src/modules/editor/TransformWidget.cpp
    src/modules/editor/TransformWidget.h
)
target_compile_definitions(EngineLabs PRIVATE ENGINE_ASSET_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets" ENGINE_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
enable_hardware_ray_tracing(EngineLabs)
target_include_directories(EngineLabs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(EngineLabs PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets OpenGL::GL)

include(CTest)
if(BUILD_TESTING)
    set(SCENE_TEST_SOURCES
        src/modules/componentsBackend/LightComponent/LightComponent.cpp
        src/modules/scene/Scene.cpp
        src/modules/scene/SceneSerialization.cpp
        src/modules/scene/SceneObject.cpp
        src/modules/componentsBackend/TransformComponent/TransformComponent.cpp
        src/modules/componentsBackend/TerrainComponent/TerrainComponent.cpp
        src/modules/componentsBackend/MeshComponent/MeshComponent.cpp
        src/modules/assets/MeshImporter.cpp
        src/modules/material/Material.cpp
    )
    add_executable(EditorLightingTests tests/EditorLightingTests.cpp src/modules/editor/TransformGizmo.cpp src/modules/project/ProjectFile.cpp ${SCENE_TEST_SOURCES})
    target_include_directories(EditorLightingTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(EditorLightingTests PRIVATE Qt6::Widgets Qt6::OpenGL)
    add_test(NAME EditorLighting COMMAND EditorLightingTests)
    set_tests_properties(EditorLighting PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(TerrainTests tests/TerrainTests.cpp src/modules/project/ProjectFile.cpp ${SCENE_TEST_SOURCES})
    target_include_directories(TerrainTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(TerrainTests PRIVATE ENGINE_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
    target_link_libraries(TerrainTests PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL)
    add_test(NAME Terrain COMMAND TerrainTests)
    add_executable(HardwareRayTracingTests tests/HardwareRayTracingTests.cpp
        src/modules/renderer/VulkanRayTracer.cpp src/modules/renderer/SoftwareRayTracer.cpp
        assets/shaders/shaders.qrc ${SCENE_TEST_SOURCES})
    target_include_directories(HardwareRayTracingTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(HardwareRayTracingTests PRIVATE ENGINE_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
    target_link_libraries(HardwareRayTracingTests PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL)
    enable_hardware_ray_tracing(HardwareRayTracingTests)
    add_test(NAME HardwareRayTracing COMMAND HardwareRayTracingTests)
    set_tests_properties(HardwareRayTracing PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(RenderingTests tests/RenderingTests.cpp
        src/modules/editor/TransformGizmo.cpp
        src/modules/renderer/Renderer.cpp
        src/modules/renderer/GPUMesh.cpp
        src/modules/renderer/PostProcessor.cpp
        src/modules/renderer/HybridLighting.cpp
        src/modules/editor/LightingDemo.cpp
        src/modules/renderer/ShadowMap.cpp
        src/modules/renderer/HardwareViewport.cpp
        src/modules/renderer/VulkanRayTracer.cpp
        src/modules/renderer/SoftwareRayTracer.cpp
        src/modules/resources/ResourceManager.cpp
        src/modules/shader/Shader.cpp
        src/modules/shader/ShaderAsset.cpp
        assets/shaders/shaders.qrc
        ${SCENE_TEST_SOURCES})
    target_include_directories(RenderingTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(RenderingTests PRIVATE ENGINE_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
    target_link_libraries(RenderingTests PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL OpenGL::GL)
    enable_hardware_ray_tracing(RenderingTests)
    add_test(NAME Rendering COMMAND RenderingTests)
    add_executable(ProjectAndPrefabTests tests/ProjectAndPrefabTests.cpp src/modules/project/ProjectFile.cpp ${SCENE_TEST_SOURCES})
    target_include_directories(ProjectAndPrefabTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(ProjectAndPrefabTests PRIVATE ENGINE_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
    target_link_libraries(ProjectAndPrefabTests PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL)
    add_test(NAME ProjectAndPrefab COMMAND ProjectAndPrefabTests)
    add_executable(SoftwareRayTracerTests tests/SoftwareRayTracerTests.cpp src/modules/renderer/SoftwareRayTracer.cpp ${SCENE_TEST_SOURCES})
    target_include_directories(SoftwareRayTracerTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(SoftwareRayTracerTests PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL)
    add_test(NAME SoftwareRayTracer COMMAND SoftwareRayTracerTests)
    add_executable(ScenePersistenceTests
        tests/ScenePersistenceTests.cpp
        src/modules/componentsBackend/LightComponent/LightComponent.cpp
        src/modules/scene/Scene.cpp
        src/modules/scene/SceneSerialization.cpp
        src/modules/scene/SceneObject.cpp
        src/modules/componentsBackend/TransformComponent/TransformComponent.cpp
        src/modules/componentsBackend/TerrainComponent/TerrainComponent.cpp
        src/modules/componentsBackend/MeshComponent/MeshComponent.cpp
        src/modules/assets/MeshImporter.cpp
        src/modules/material/Material.cpp
    )
    target_include_directories(ScenePersistenceTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(ScenePersistenceTests PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL)
    add_test(NAME ScenePersistence COMMAND ScenePersistenceTests)
    add_executable(ShaderAssetTests
        tests/ShaderAssetTests.cpp
        src/modules/shader/ShaderAsset.cpp
        src/modules/shader/Shader.cpp
        src/modules/material/Material.cpp
        src/modules/resources/ResourceManager.cpp
    )
    target_include_directories(ShaderAssetTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(ShaderAssetTests PRIVATE ENGINE_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
    target_link_libraries(ShaderAssetTests PRIVATE Qt6::Gui Qt6::OpenGL)
    add_test(NAME ShaderAssets COMMAND ShaderAssetTests)
    add_executable(CameraTests tests/CameraTests.cpp)
    target_include_directories(CameraTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(CameraTests PRIVATE Qt6::Gui)
    add_test(NAME Camera COMMAND CameraTests)
    add_executable(MeshCreationTests
        tests/MeshCreationTests.cpp
        src/modules/material/Material.cpp
        src/modules/componentsBackend/LightComponent/LightComponent.cpp
        src/modules/scene/Scene.cpp
        src/modules/scene/SceneSerialization.cpp
        src/modules/componentsBackend/TransformComponent/TransformComponent.cpp
        src/modules/scene/SceneObject.cpp
        src/modules/componentsBackend/TerrainComponent/TerrainComponent.cpp
        src/modules/componentsBackend/MeshComponent/MeshComponent.cpp
        src/modules/assets/MeshImporter.cpp
    )
    target_include_directories(MeshCreationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(MeshCreationTests PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL)
    add_test(NAME MeshCreation COMMAND MeshCreationTests ${CMAKE_CURRENT_BINARY_DIR}/mesh-test-data)
endif()

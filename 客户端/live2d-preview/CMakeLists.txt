cmake_minimum_required(VERSION 3.24)
project(Live2DPetPreview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CUBISM_SDK "D:/CubismSdkForNative-5-r.5" CACHE PATH "Cubism SDK for Native root")
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build this preview with MSVC x64")
endif()
# The supplied R5 Core uses the v143 binary ABI; VS 2026 links it with the
# newer compatible MSVC toolset. Keep this choice explicit, not auto-guessed.
set(CORE_LIB "${CUBISM_SDK}/Core/lib/windows/x86_64/143")
if(NOT EXISTS "${CORE_LIB}/Live2DCubismCore_MD.lib")
  message(FATAL_ERROR "Missing Windows Core library in ${CUBISM_SDK}")
endif()
add_library(Live2DCubismCore STATIC IMPORTED)
set_target_properties(Live2DCubismCore PROPERTIES
  IMPORTED_LOCATION "${CORE_LIB}/Live2DCubismCore_MD.lib"
  IMPORTED_LOCATION_DEBUG "${CORE_LIB}/Live2DCubismCore_MDd.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${CUBISM_SDK}/Core/include")
set(FRAMEWORK_SOURCE D3D11)
add_subdirectory("${CUBISM_SDK}/Framework" "${CMAKE_BINARY_DIR}/Framework")
target_link_libraries(Framework PUBLIC Live2DCubismCore d3d11 d3dcompiler)
target_compile_options(Framework PRIVATE /utf-8 /MP)
add_executable(Live2DPetPreview WIN32 main.cpp)
target_compile_definitions(Live2DPetPreview PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(Live2DPetPreview PRIVATE /W4 /utf-8 /EHsc)
target_link_libraries(Live2DPetPreview PRIVATE Framework windowscodecs ole32 shell32)
set_target_properties(Live2DPetPreview PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
add_custom_target(PreviewResources ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CUBISM_SDK}/Framework/src/Rendering/D3D11/Shaders"
    "$<TARGET_FILE_DIR:Live2DPetPreview>/FrameworkShaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets/live2d/runtime/pet"
    "$<TARGET_FILE_DIR:Live2DPetPreview>/Resources/pet"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Live2DPetPreview>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CUBISM_SDK}/LICENSE.md"
    "$<TARGET_FILE_DIR:Live2DPetPreview>/licenses/SDK-LICENSE.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CUBISM_SDK}/Core/LICENSE.md"
    "$<TARGET_FILE_DIR:Live2DPetPreview>/licenses/Core-LICENSE.md")
add_dependencies(PreviewResources Live2DPetPreview)

# The C desktop host loads this C-ABI bridge; its existing MinGW build stays intact.
add_library(PetLive2D SHARED ../src/live2d_bridge.cpp)
target_compile_definitions(PetLive2D PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(PetLive2D PRIVATE /W4 /utf-8 /EHsc)
target_link_libraries(PetLive2D PRIVATE Framework windowscodecs ole32)
set_target_properties(PetLive2D PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
add_custom_target(DesktopLive2DResources ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-runtime"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:PetLive2D>" "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-runtime/PetLive2D.dll"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CUBISM_SDK}/Framework/src/Rendering/D3D11/Shaders" "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-runtime/FrameworkShaders"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-runtime/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CUBISM_SDK}/LICENSE.md" "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-runtime/licenses/SDK-LICENSE.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CUBISM_SDK}/Core/LICENSE.md" "${CMAKE_CURRENT_SOURCE_DIR}/../live2d-runtime/licenses/Core-LICENSE.md")
add_dependencies(DesktopLive2DResources PetLive2D)

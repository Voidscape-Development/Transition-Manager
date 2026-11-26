cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Transition Manager is a Qt dock driven through the frontend API, so both are
# required rather than optional.
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_BUILD_LEGACY "Enable legacy build" OFF)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "Transition Manager requires ENABLE_FRONTEND_API=ON and ENABLE_QT=ON")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

# Transition Manager: the dock and window. Always built.
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/manager/manager-hooks.hpp
    src/manager/manager-hooks.cpp
    src/manager/manager-module.hpp
    src/manager/manager-module.cpp
    src/manager/obs-qt-types.hpp
    src/manager/transition-store.hpp
    src/manager/transition-store.cpp
    src/manager/transition-preview.hpp
    src/manager/transition-preview.cpp
    src/manager/properties-panel.hpp
    src/manager/properties-panel.cpp
    src/manager/transition-manager-window.hpp
    src/manager/transition-manager-window.cpp
    src/manager/transition-dialogs.hpp
    src/manager/transition-dialogs.cpp
    src/manager/transitions-dock.hpp
    src/manager/transitions-dock.cpp
    src/manager/tm-style.hpp
    src/manager/tm-style.cpp
    src/manager/transition-list.hpp
    src/manager/transition-list.cpp
    src/manager/timeline-slider.hpp
    src/manager/media-probe.hpp
    src/manager/media-probe.cpp
    src/manager/stinger-analyzer.hpp
    src/manager/stinger-analyzer.cpp
    src/manager/transition-pack.hpp
    src/manager/transition-pack.cpp
    src/actions/transition-actions.hpp
    src/actions/transition-actions.cpp
    src/actions/transition-actions-editor.cpp
)

# Transition Tree: per-switch transition rules, plus the bridge that links it
# with the manager. Turn off to build the manager on its own.
option(ENABLE_TRANSITION_TREE "Include Transition Tree's scene-switch rules" ON)

if(ENABLE_TRANSITION_TREE)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TM_WITH_TRANSITION_TREE)
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/tree/obs-websocket-api.h
      src/tree/transition-tree.hpp
      src/tree/transition-tree-dialog.hpp
      src/tree/transition-tree-dialog.cpp
      src/tree/transition-tree-engine.cpp
      src/tree/transition-tree-integration.cpp
      src/tree/transition-tree-model.cpp
      src/tree/transition-tree-websocket.cpp
      src/bridge/bridge.hpp
      src/bridge/bridge.cpp
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

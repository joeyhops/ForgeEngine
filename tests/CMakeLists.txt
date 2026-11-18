find_package(Catch2 3 CONFIG QUIET)
if(Catch2_FOUND)
  message(STATUS "forge_tests: Catch2 ${Catch2_VERSION} (find_package)")
  list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
else()
  message(STATUS "forge_tests: Catch2 not found - fetching v3.14.0")
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.14.0
    GIT_SHALLOW TRUE
    SYSTEM
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)

set(FORGE_TEST_SUPPORT
  support/TestPaths.cpp
  support/LogCapture.cpp
  support/ForgeTestListener.cpp
  support/TempDir.cpp
  support/AnimTestUtils.cpp
  support/GraphBuilder.cpp
  support/NodeTicker.cpp
  support/Golden.cpp
  support/Baseline.cpp
  support/ContentLint.cpp
)

set(FORGE_TEST_UNIT
  unit/test_TypeSystem.cpp
  unit/test_SyncTrack.cpp
  unit/test_SampledEvents.cpp
  unit/test_AnimParamsAndFsm.cpp
  unit/test_TaskSystem.cpp
  unit/test_BoneSampling.cpp
  unit/test_EventBus.cpp
  unit/test_FlagManager.cpp
  unit/test_RootMotionPayload.cpp
)

set(FORGE_TEST_COMPONENT
  component/test_ClipNode.cpp
  component/test_Blend1D.cpp
  component/test_Blend2D.cpp
  component/test_ClipSelector.cpp
  component/test_StateMachineNode.cpp
  component/test_GraphInstance.cpp
  component/test_GraphLoader.cpp
  component/test_TAESerialization.cpp
  component/test_CombatComponent.cpp
  component/test_CombatSystem.cpp
  component/test_Animator.cpp
  component/test_TriggerVolume.cpp
  component/test_MapGeometry.cpp
  component/test_AssetDefs.cpp
)

set(FORGE_TEST_CONTENT
  content/test_ContentGraphs.cpp
  content/test_ContentTae.cpp
  content/test_ContentDefs.cpp
  content/test_ContentScripts.cpp
  content/test_ContentClips.cpp
)

set(FORGE_TEST_SCENARIO
  scenario/test_ScenarioPlayerGraph.cpp
)

add_executable(forge_tests
  ${FORGE_TEST_SUPPORT}
  ${FORGE_TEST_UNIT}
  ${FORGE_TEST_COMPONENT}
  ${FORGE_TEST_CONTENT}
  ${FORGE_TEST_SCENARIO}
)

target_include_directories(forge_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(forge_tests PRIVATE Catch2::Catch2WithMain)
forge_link_engine(forge_tests)
forge_enable_warnings(forge_tests)

target_compile_definitions(forge_tests PRIVATE
  FORGE_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}/"
  FORGE_TEST_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets/"
  FORGE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/"
  FORGE_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/"
  FORGE_TEST_BASELINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/baselines/"
  FORGE_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}/"
)

catch_discover_tests(forge_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES TIMEOUT 120
)


project(multiagent
  LANGUAGES CXX
)
add_library(${PROJECT_NAME})

set(INCLUDE_DIR include/universal-pddl/multiagent)

#generate_export_header(${PROJECT_NAME} 
#  EXPORT_MACRO_NAME
#    ICL_CORE_IMPORT_EXPORT
#  EXPORT_FILE_NAME
#    "${CMAKE_CURRENT_SOURCE_DIR}/${INCLUDE_DIR}/ImportExport.h"
#)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/universal-pddl>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_sources(${PROJECT_NAME}
  PRIVATE    
    src/AgentAction.cpp    
    src/ConcurrencyGround.cpp
    src/ConcurrencyPredicate.cpp
    src/ConcurrentAction.cpp
    src/NetworkNode.cpp
  PUBLIC FILE_SET HEADERS 
  BASE_DIRS ${INCLUDE_DIR}
  FILES
    ${INCLUDE_DIR}/AgentAction.h
    ${INCLUDE_DIR}/ConcurrencyDomain.h
    ${INCLUDE_DIR}/ConcurrencyGround.h
    ${INCLUDE_DIR}/ConcurrencyPredicate.h
    ${INCLUDE_DIR}/ConcurrentAction.h
    ${INCLUDE_DIR}/MultiagentDomain.h
    ${INCLUDE_DIR}/NetworkNode.h
#    ${INCLUDE_DIR}/ImportExport.h
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_link_libraries(${PROJECT_NAME}
  PUBLIC
    parser
)

install(
  TARGETS 
    ${PROJECT_NAME}
  #EXPORT
  #  gpu_voxels-targets
  LIBRARY
    DESTINATION lib
  FILE_SET HEADERS
    DESTINATION ${INCLUDE_DIR}
)
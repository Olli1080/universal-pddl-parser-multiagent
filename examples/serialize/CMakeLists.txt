add_executable(serialize serialize.cpp)
target_link_libraries(serialize
    PUBLIC
        multiagent
)

target_compile_features(serialize PUBLIC cxx_std_20)

install(
  TARGETS 
    serialize
  #EXPORT
  #  gpu_voxels-targets
  LIBRARY
    DESTINATION lib
  FILE_SET HEADERS
    DESTINATION ${INCLUDE_DIR}
)
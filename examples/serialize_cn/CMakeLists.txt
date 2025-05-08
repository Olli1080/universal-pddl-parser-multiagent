add_executable(serialize_cn serialize.cpp)
target_link_libraries(serialize_cn
    PUBLIC
        multiagent
)

target_compile_features(serialize_cn PUBLIC cxx_std_20)

add_executable(compress_cn compress.cpp)
target_link_libraries(compress_cn
    PUBLIC
        multiagent
)

target_compile_features(compress_cn PUBLIC cxx_std_20)

install(
  TARGETS 
    serialize_cn
    compress_cn
  #EXPORT
  #  gpu_voxels-targets
  LIBRARY
    DESTINATION lib
  FILE_SET HEADERS
    DESTINATION ${INCLUDE_DIR}
)
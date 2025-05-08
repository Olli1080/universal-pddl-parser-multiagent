include(CTest)

file(COPY expected DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY "../domains" DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(MultiagentTests tests.cpp)
target_link_libraries(MultiagentTests
    PUBLIC
        multiagent
        GTest::gtest_main
)

target_compile_features(MultiagentTests PUBLIC cxx_std_20)
gtest_discover_tests(MultiagentTests)
#add_test(NAME DomainTests
#         COMMAND DomainTests)
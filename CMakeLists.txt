cmake_minimum_required(VERSION 3.23)
include(GenerateExportHeader)
include(GNUInstallDirs)

project(universal-pddl-parser-multiagent
    LANGUAGES CXX)

add_subdirectory(universal-pddl-parser)
add_subdirectory(multiagent)
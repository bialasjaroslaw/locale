cmake_minimum_required(VERSION 3.14)

project(locales LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(QT_BACKEND "Add Qt for comparison" ON)

if(QT_BACKEND)
    find_package(Qt5 COMPONENTS Core Location CONFIG REQUIRED)
endif()

add_subdirectory(magic_enum)

add_executable(locales
  main.cpp
)
target_link_libraries(locales PUBLIC 
    fmt 
    boost_locale 
    icui18n
    icuuc 
    magic_enum
)

if(QT_BACKEND)
    target_link_libraries(locales PUBLIC Qt5::Core Qt5::Location)
    target_compile_definitions(locales PUBLIC QT_BACKEND)
endif()

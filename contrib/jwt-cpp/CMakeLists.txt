cmake_minimum_required(VERSION 3.14)

project(jwt-cpp
    LANGUAGES CXX)

add_library(jwt-cpp INTERFACE)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.23.0)
    target_sources(jwt-cpp
        INTERFACE
            FILE_SET HEADERS
            BASE_DIRS include/
            FILES
                picojson/picojson.h
                jwt-cpp/base.h
                jwt-cpp/traits/boost-json/defaults.h
                jwt-cpp/traits/boost-json/traits.h
                jwt-cpp/traits/danielaparker-jsoncons/defaults.h
                jwt-cpp/traits/danielaparker-jsoncons/traits.h
                jwt-cpp/traits/nlohmann-json/defaults.h
                jwt-cpp/traits/nlohmann-json/traits.h
                jwt-cpp/traits/kazuho-picojson/defaults.h
                jwt-cpp/traits/kazuho-picojson/traits.h
                jwt-cpp/traits/defaults.h.mustache
                jwt-cpp/jwt.h)

else()
    target_include_directories(jwt-cpp INTERFACE
        include/)
endif()

cmake_minimum_required(VERSION 3.22.1)
project(fullthrottle CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp)
add_library(fullthrottle SHARED android_main.cpp ${CORE_SRC})
target_compile_options(fullthrottle PRIVATE -O2 -ffast-math -Wall -Wno-unused-function)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
target_link_libraries(fullthrottle native_app_glue android EGL GLESv3 aaudio log m)

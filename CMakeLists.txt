cmake_minimum_required(VERSION 3.16)

# UTF-8 source
add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

set(PROJECT_NAME httprequest)
set(PROJECT_VERSION 1.00)

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

# すべてのビルドタイプ (Debug/Release 等) で静的ランタイムを使用する
# Debug ビルドは /MTd、Release ビルドは /MT になる
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT TARGET ncbind)
add_subdirectory(../ncbind ${CMAKE_CURRENT_BINARY_DIR}/ncbind)
endif()

add_library(${PROJECT_NAME} SHARED
	Base64.cpp
	encoding.cpp
	HttpConnection.cpp
	HttpConnectionWinHttp.cpp
	HttpConnectionWinINet.cpp
	match.cpp
	Main.cpp
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ncbind
)

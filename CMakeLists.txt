cmake_minimum_required(VERSION 3.21)

project(hse_socks5
	VERSION 1.0
	LANGUAGES CXX
)

add_executable(${PROJECT_NAME}
	src/main.cpp
	src/client.cpp
	src/server.cpp
	src/log.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
	CXX_EXTENSIONS OFF
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(${PROJECT_NAME} PRIVATE include)

set(BUILD_UVW_LIBS ON)
add_subdirectory(uvw)
target_link_libraries(${PROJECT_NAME} PRIVATE uvw::uvw-static)

#项目根目录下的CmakeLists.txt文件
cmake_minimum_required(VERSION 3.0)
project(conn_pool)

#配置编译选项
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11 -g -Wall -Werror")

#配置最终的可执行文件的输出路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

#配置头文件的搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)

#加载子目录
add_subdirectory(src)
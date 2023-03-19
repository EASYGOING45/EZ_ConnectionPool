#定义了一个SRC_LIST变量，包含了该目录下所有的源文件
aux_source_directory(. SRC_LIST)

#指定生成可执行文件
add_executable(Conn_Pool ${SRC_LIST})

#指定所需要以来的库文件
target_link_libraries(Conn_Pool mysqlclient pthread)
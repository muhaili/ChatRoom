cmake_minimum_required(VERSION 3.0)
project(ChatServer)

# 1. 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -Wall")

# 2. 包含路径
include_directories(/usr/include/mysql)

# 3. 【核心修改】手动指定服务端的源文件，排除掉 client.cpp
# 注意：这里列出你所有服务端的 .cpp
set(SERVER_SRC 
    main.cpp 
    db.cpp 
    server.cpp
)

# 4. 生成服务端
add_executable(ChatServer ${SERVER_SRC})
target_link_libraries(ChatServer 
    muduo_net 
    muduo_base 
    pthread 
    mysqlclient
)

# 5. 生成客户端
add_executable(ChatClient client.cpp)
target_link_libraries(ChatClient pthread)
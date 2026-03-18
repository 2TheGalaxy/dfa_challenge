cmake_minimum_required(VERSION 3.20)
project(dfa_challenge CXX)

set(CMAKE_CXX_STANDARD 17)

# Możemy ustawić, który target kompilować
option(CLIENT "Compile client executable" ON)
option(SERVER "Compile server executable" OFF)

if(WIN32)
    set(SOCKET_LIBS ws2_32 wsock32)
endif()

# Klient
if(CLIENT)
    add_executable(dfa_client main_client.cpp)
    target_include_directories(dfa_client PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(dfa_client PRIVATE ${SOCKET_LIBS} )
endif()

# Server
if(SERVER)
    add_executable(dfa_server main_server.cpp)
    target_include_directories(dfa_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(dfa_server PRIVATE ${SOCKET_LIBS} )
endif()

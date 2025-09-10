#CMakeLists.txt для клиента (client/CMakeLists.txt):
find_package(Qt6 COMPONENTS Core Network REQUIRED)

add_executable(NetworkClient
    main.cpp
    include/client.h
    src/client.cpp
    include/networkmetrics.h
    src/networkmetrics.cpp
    include/devicestatus.h
    src/devicestatus.cpp
    include/loggenerator.h
    src/loggenerator.cpp
)

target_include_directories(NetworkClient PRIVATE
    include
)

target_link_libraries(NetworkClient PRIVATE
    Qt6::Core
    Qt6::Network
)



find_package(Qt6 COMPONENTS Core Gui Widgets Network REQUIRED)

add_executable(NetworkServer
    main.cpp
    GUI/include/mainwindow.h
    GUI/src/mainwindow.cpp
    GUI/include/settingsdialog.h
    GUI/src/settingsdialog.cpp
    Server/include/serverthread.h
    Server/src/serverthread.cpp
    Server/include/serverworker.h
    Server/src/serverworker.cpp
)

target_include_directories(NetworkServer PRIVATE
    GUI/include
    Server/include
)

target_link_libraries(NetworkServer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)


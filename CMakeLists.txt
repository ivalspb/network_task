
# Главный CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(NetworkSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Настройка для обоих подпроектов
set(CMAKE_AUTOMOC ON)

# Добавление подпроекта клиента
add_subdirectory(client)

# Добавление подпроекта сервера
add_subdirectory(server)

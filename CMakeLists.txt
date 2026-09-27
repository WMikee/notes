cmake_minimum_required(VERSION 3.21)
project(notes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

add_executable(notes
    main.cpp
    stroke.h stroke.cpp
    image.h image.cpp
    text.h text.cpp
    shape.h shape.cpp
    camera.h camera.cpp
    document.h document.cpp
    select.h select.cpp
    shortcuts.h shortcuts.cpp
    canvas.h canvas.cpp
    anim.h
    library.h library.cpp
    sidepanel.h sidepanel.cpp
    toolbutton.h toolbutton.cpp
    mainwindow.h mainwindow.cpp
    clipboard.h clipboard.cpp
    storage.h storage.cpp
    shaders.qrc
    assets.qrc
)
target_link_libraries(notes PRIVATE Qt6::Widgets Qt6::OpenGLWidgets)
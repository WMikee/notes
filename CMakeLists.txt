cmake_minimum_required(VERSION 3.21)
project(notes LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

add_executable(notes
    src/app/main.cpp
    src/app/mainwindow.h src/app/mainwindow.cpp
    src/app/shortcuts.h src/app/shortcuts.cpp
    src/canvas/canvas.h src/canvas/canvas.cpp
    src/canvas/glrenderer.h src/canvas/glrenderer.cpp
    src/canvas/camera.h src/canvas/camera.cpp
    src/scene/document.h src/scene/document.cpp
    src/scene/stroke.h src/scene/stroke.cpp src/scene/xform.h
    src/scene/shape.h src/scene/shape.cpp
    src/scene/text.h src/scene/text.cpp
    src/scene/select.h src/scene/select.cpp
    src/scene/image.h src/scene/image.cpp
    src/io/storage.h src/io/storage.cpp
    src/io/clipboard.h src/io/clipboard.cpp
    src/io/pdfexport.h src/io/pdfexport.cpp
    src/io/library.h src/io/library.cpp
    src/tools/tool.h
    src/tools/selecttool.h src/tools/selecttool.cpp
    src/ui/sizeslider.h src/ui/sizeslider.cpp
    src/ui/sizenumberfield.h src/ui/sizenumberfield.cpp
    src/ui/sidepanel.h src/ui/sidepanel.cpp
    src/ui/pagepicker.h src/ui/pagepicker.cpp
    src/ui/toolbutton.h src/ui/toolbutton.cpp
    src/theme.h
    src/ui/anim.h
    shaders.qrc
    assets.qrc
)

target_include_directories(notes PRIVATE src)
target_link_libraries(notes PRIVATE Qt6::Widgets Qt6::OpenGLWidgets)

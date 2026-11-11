cmake_minimum_required(VERSION 3.16)
project(TrayNotes LANGUAGES CXX)

add_executable(TrayNotes WIN32 main.cpp)
target_compile_features(TrayNotes PRIVATE cxx_std_17)
target_link_libraries(TrayNotes PRIVATE user32 gdi32 shell32 comctl32 ole32 uuid dwmapi uxtheme)

cmake_minimum_required(VERSION 3.20)

project(darkfire)

set(CMAKE_CXX_STANDARD 20)

add_executable(dark src/main.cpp
        src/tokenization.h
        src/parser.h
        src/generator.h)

#hydro = dark
#.hy = .fire
#hydrogen = darkfire
cmake_minimum_required(VERSION 3.10.0)
project(ControlChainEmulator VERSION 0.1.0 LANGUAGES C CXX)

include_directories(.)
include_directories(cc-slave/src)

add_executable(ControlChainEmulator 
    main.cpp
    cc-slave/src/actuator.c
    cc-slave/src/assignment.c
    cc-slave/src/core.c
    cc-slave/src/device.c
    cc-slave/src/handshake.c
    cc-slave/src/msg.c
    cc-slave/src/update.c
    cc-slave/src/utils.c
)

 
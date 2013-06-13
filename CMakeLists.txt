# @@@LICENSE
#
# Copyright (c) 2012-2013 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

cmake_minimum_required(VERSION 2.8.7)

project(novacom C)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC4)
webos_component(2 0 0)

include_directories(src)
file(GLOB SOURCE_FILES src/*.c)
add_executable(${CMAKE_PROJECT_NAME} ${SOURCE_FILES})
add_definitions(-DBUILDVERSION="${WEBOS_COMPONENT_VERSION}")

webos_add_compiler_flags(ALL -Wall -Wno-multichar -Wno-unused-parameter -Wno-unused-function -g)

target_link_libraries(${CMAKE_PROJECT_NAME} pthread)

webos_build_program()

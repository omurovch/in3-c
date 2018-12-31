set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/test/cmake/")

#find_package(Check REQUIRED)
#include_directories(${CHECK_INCLUDE_DIR} ${CHECK_INCLUDE_DIRS})

include_directories(. ../src)

#add_executable(in3_tests core.c)
#target_link_libraries(in3_tests ${CHECK_LIBRARIES} core in3_eth_nano)

add_executable(runner runner.c)
target_link_libraries(runner core in3_eth_full)


file(GLOB files "testdata/*.json")
foreach(file ${files})
  get_filename_component(testname "${file}" NAME_WE)
  add_test(
    NAME              ${testname} 
    COMMAND           ${CMAKE_CURRENT_BINARY_DIR}/runner ${file} 
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/..
)
endforeach()



#add_test(eth_tx ${CMAKE_CURRENT_BINARY_DIR}/runner receipt)

#set_property(TARGET in3_tests PROPERTY C_STANDARD 99)
set_property(TARGET runner PROPERTY C_STANDARD 99)

set(UT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/ut)

include_directories(${CMAKE_SOURCE_DIR}/include/neuron)

aux_source_directory(${CMAKE_SOURCE_DIR}/src/parser SRC_PARSER)
add_executable(json_test json_test.cc)
target_include_directories(json_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(json_test neuron-base gtest_main gtest pthread jansson)

add_executable(http_test http_test.cc 
	${CMAKE_SOURCE_DIR}/src/utils/http.c)
	
target_include_directories(http_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/plugins/restful)
target_link_libraries(http_test neuron-base gtest_main gtest jansson nng)

add_executable(jwt_test jwt_test.cc)
target_include_directories(jwt_test PRIVATE 
    ${CMAKE_SOURCE_DIR}/src 
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/src/utils)
target_link_libraries(jwt_test neuron-base gtest_main gtest jansson ssl crypto jwt libzlog.so)

add_executable(base64_test base64_test.cc ${CMAKE_SOURCE_DIR}/src/utils/base64.c)
target_include_directories(base64_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include       
)
target_link_libraries(base64_test neuron-base gtest_main gtest crypto)

aux_source_directory(${CMAKE_SOURCE_DIR}/src/base/tag_sort.c SRC_SORT)
add_executable(tag_sort_test tag_sort_test.cc ${SRC_SORT})
target_include_directories(tag_sort_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(tag_sort_test neuron-base gtest_main gtest)

add_executable(modbus_test modbus_test.cc
				${CMAKE_SOURCE_DIR}/plugins/modbus/modbus.c
				${CMAKE_SOURCE_DIR}/plugins/modbus/modbus_point.c)
target_include_directories(modbus_test PRIVATE
				${CMAKE_SOURCE_DIR}/plugins/modbus)
target_link_libraries(modbus_test neuron-base gtest_main gtest pthread zlog)

add_executable(async_queue_test async_queue_test.cc 
	${CMAKE_SOURCE_DIR}/src/utils/async_queue.c)
target_include_directories(async_queue_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src/utils
	${CMAKE_SOURCE_DIR}/include)
target_link_libraries(async_queue_test neuron-base gtest_main gtest)

add_executable(rolling_counter_test rolling_counter_test.cc)
target_include_directories(rolling_counter_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(rolling_counter_test neuron-base gtest_main gtest)

add_executable(mqtt_client_test mqtt_client_test.cc)
target_include_directories(mqtt_client_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mqtt_client_test neuron-base gtest_main gtest)


add_executable(common_test common_test.cc)
target_include_directories(common_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(common_test neuron-base gtest_main gtest)

file(COPY ${CMAKE_SOURCE_DIR}/tests/ut/serverBMS_3_test.cid DESTINATION ${UT_DIRECTORY}/config)
add_executable(cid_test cid_test.cc)
target_include_directories(cid_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(cid_test neuron-base gtest_main gtest)

add_executable(mqtt_schema_test mqtt_schema_test.cc ${CMAKE_SOURCE_DIR}/plugins/mqtt/schema.c)
target_include_directories(mqtt_schema_test PRIVATE 
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/plugins
)
target_link_libraries(mqtt_schema_test neuron-base gtest_main gtest)

include(GoogleTest)
# gtest_discover_tests(json_test)
# gtest_discover_tests(http_test)
# gtest_discover_tests(jwt_test)
# gtest_discover_tests(base64_test)
# gtest_discover_tests(tag_sort_test)
# gtest_discover_tests(modbus_test)
# gtest_discover_tests(async_queue_test)
# gtest_discover_tests(rolling_counter_test)
# gtest_discover_tests(mqtt_client_test)
# gtest_discover_tests(common_test)
# gtest_discover_tests(cid_test)
# gtest_discover_tests(mqtt_schema_test)

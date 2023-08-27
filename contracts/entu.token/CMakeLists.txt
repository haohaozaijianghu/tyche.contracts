add_contract(entu.token entu.token ${CMAKE_CURRENT_SOURCE_DIR}/src/entu.token.cpp)

target_include_directories(entu.token
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(entu.token
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/entu.token.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/entu.token.contracts.md @ONLY )

target_compile_options( entu.token PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

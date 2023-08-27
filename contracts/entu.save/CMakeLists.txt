add_contract(entu.save entu.save ${CMAKE_CURRENT_SOURCE_DIR}/src/entu.save.cpp)

target_include_directories(entu.save
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(entu.save
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/entu.save.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/entu.save.contracts.md @ONLY )

target_compile_options( entu.save PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

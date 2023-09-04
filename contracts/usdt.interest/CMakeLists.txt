add_contract(usdt.interest usdt.interest ${CMAKE_CURRENT_SOURCE_DIR}/src/usdt.interest.cpp)

target_include_directories(usdt.interest
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(usdt.interest
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/usdt.interest.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/usdt.interest.contracts.md @ONLY )

target_compile_options( usdt.interest PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

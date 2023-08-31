add_contract(usdt.save usdt.save ${CMAKE_CURRENT_SOURCE_DIR}/src/usdt.save.cpp)

target_include_directories(usdt.save
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(usdt.save
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/usdt.save.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/usdt.save.contracts.md @ONLY )

target_compile_options( usdt.save PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

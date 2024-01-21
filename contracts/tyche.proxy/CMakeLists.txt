add_contract(tyche.proxy tyche.proxy ${CMAKE_CURRENT_SOURCE_DIR}/src/tyche.proxy.cpp)

target_include_directories(tyche.proxy
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.proxy)

set_target_properties(tyche.proxy
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/tyche.proxy.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/tyche.proxy.contracts.md @ONLY )

target_compile_options( tyche.proxy PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

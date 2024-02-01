add_contract(tyche.swap tyche.swap ${CMAKE_CURRENT_SOURCE_DIR}/src/tyche.swap.cpp)

target_include_directories(tyche.swap
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.swap
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.reward)

set_target_properties(tyche.swap
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/tyche.swap.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/tyche.swap.contracts.md @ONLY )

target_compile_options( tyche.swap PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

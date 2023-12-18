add_contract(tyche.stake tyche.stake ${CMAKE_CURRENT_SOURCE_DIR}/src/tyche.stake.cpp)

target_include_directories(tyche.stake
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.stake
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.reward)

set_target_properties(tyche.stake
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/tyche.stake.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/tyche.stake.contracts.md @ONLY )

target_compile_options( tyche.stake PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

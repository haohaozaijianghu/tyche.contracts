add_contract(tyche.earn tyche.earn ${CMAKE_CURRENT_SOURCE_DIR}/src/tyche.earn.cpp)

target_include_directories(tyche.earn
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.earn
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.reward)

set_target_properties(tyche.earn
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/tyche.earn.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/tyche.earn.contracts.md @ONLY )

target_compile_options( tyche.earn PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

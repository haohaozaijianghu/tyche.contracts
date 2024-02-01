add_contract(tyche.lock tyche.lock ${CMAKE_CURRENT_SOURCE_DIR}/src/tyche.lock.cpp)

target_include_directories(tyche.lock
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.lock
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.reward)

set_target_properties(tyche.lock
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/tyche.lock.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/tyche.lock.contracts.md @ONLY )

target_compile_options( tyche.lock PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

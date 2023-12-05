add_contract(price.oracle price.oracle
   ${CMAKE_CURRENT_SOURCE_DIR}/src/price.oracle.cpp
)

target_include_directories(price.oracle
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(price.oracle
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/price.oracle.clauses.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/price.oracle.clauses.md @ONLY )
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/price.oracle.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/price.oracle.contracts.md @ONLY )

target_compile_options( price.oracle PUBLIC -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

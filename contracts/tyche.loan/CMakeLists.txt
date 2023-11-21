add_contract(tyche.loan tyche.loan ${CMAKE_CURRENT_SOURCE_DIR}/src/tyche.loan.cpp)

target_include_directories(tyche.loan
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.loan
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.reward)

set_target_properties(tyche.loan
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/tyche.loan.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/tyche.loan.contracts.md @ONLY )

target_compile_options( tyche.loan PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

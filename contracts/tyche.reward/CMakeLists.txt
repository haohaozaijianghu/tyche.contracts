add_contract(tyche.reward tyche.reward ${CMAKE_CURRENT_SOURCE_DIR}/src/tyche.reward.cpp)

target_include_directories(tyche.reward
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.earn
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.reward)

set_target_properties(tyche.reward
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/tyche.reward.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/tyche.reward.contracts.md @ONLY )

target_compile_options( tyche.reward PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

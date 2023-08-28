
add_contract(entu.swap entu.swap
${CMAKE_CURRENT_SOURCE_DIR}/src/entu.swap.cpp
)

target_include_directories(entu.swap
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   )

set_target_properties(entu.swap
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/mdao.stg.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/mdao.stg.contracts.md @ONLY )

target_compile_options( entu.swap PUBLIC -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

add_contract(tyche.market tyche.market ${CMAKE_CURRENT_SOURCE_DIR}/src/tyche.market.cpp)

target_include_directories(tyche.market
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/include/tyche.market)

set_target_properties(tyche.market
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

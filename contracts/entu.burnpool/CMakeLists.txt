# add_contract(entu entu ${CMAKE_CURRENT_SOURCE_DIR}/src/entu.cpp)

# set(VERSION_STRING "0.1.1")

# find_package(Git)
# if(GIT_FOUND)
#    execute_process(
#       COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
#       OUTPUT_VARIABLE VERSION_COMMIT_ID
#       ERROR_VARIABLE err
#       OUTPUT_STRIP_TRAILING_WHITESPACE
#       RESULT_VARIABLE res
#       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
#    )
#    if(${res} EQUAL "0")
#       set(VERSION_STRING "${VERSION_STRING}.${VERSION_COMMIT_ID}")
#    else()
#       message(WARNING "git rev-parse HEAD failed! ${err}")
#    endif()
# endif()

# configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/include/version.hpp @ONLY ESCAPE_QUOTES)

# target_include_directories(dex
#    PUBLIC
#    ${CMAKE_CURRENT_SOURCE_DIR}/include
#    ${CMAKE_CURRENT_BINARY_DIR}/include)

# set_target_properties(dex
#    PROPERTIES
#    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/entu.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/entu.contracts.md @ONLY )

# target_compile_options( entu PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

add_contract(entu.burnpool entu.burnpool
${CMAKE_CURRENT_SOURCE_DIR}/src/entu.burnpool.cpp
)

target_include_directories(entu.burnpool
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   )

set_target_properties(entu.burnpool
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/ricardian/mdao.stg.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/mdao.stg.contracts.md @ONLY )

target_compile_options( entu.burnpool PUBLIC -R${CMAKE_CURRENT_BINARY_DIR}/ricardian )

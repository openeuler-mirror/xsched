file(GLOB_RECURSE CHANGE_POLICY_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/src/*.c
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp
)
add_executable(change-xsched-policy ${CHANGE_POLICY_SRCS})

target_compile_options(change-xsched-policy PRIVATE
    -Wall
    -Wextra
    -Werror
)

install(TARGETS change-xsched-policy RUNTIME DESTINATION bin)

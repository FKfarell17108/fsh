add_subdirectory(platform)

add_executable(fsh
    main.c
    util/strbuf.c
    core/ast.c
    core/lexer.c
    core/parser.c
    core/executor.c
    env/shell_state.c
    env/variable.c
    env/alias.c
    env/fshrc.c
    highlight/highlight.c
    input/history.c
    input/line_editor.c
    builtins/builtin.c
    builtins/cmd_cd.c
    builtins/cmd_pwd.c
    builtins/cmd_exit.c
    builtins/cmd_echo.c
    builtins/cmd_type.c
    builtins/cmd_alias.c
    builtins/cmd_source.c
    builtins/cmd_clear.c
    builtins/cmd_fshrc.c
    builtins/cmd_fsh.c
    prompt/prompt.c
    prompt/git_info.c
)

target_include_directories(fsh PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fsh PRIVATE fsh_platform)

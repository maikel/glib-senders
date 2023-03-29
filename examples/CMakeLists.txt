add_executable(ex_safe_file_descriptor ex_safe_file_descriptor.cpp)
target_link_libraries(ex_safe_file_descriptor glib-senders::glib-senders)

add_executable(ex_glib_io_context ex_glib_io_context.cpp)
target_link_libraries(ex_glib_io_context glib-senders::glib-senders)

add_executable(ex_echo_coroutine ex_echo_coroutine.cpp)
target_link_libraries(ex_echo_coroutine glib-senders::glib-senders)

add_executable(ex_when_any ex_when_any.cpp)
target_link_libraries(ex_when_any glib-senders::glib-senders)

add_executable(ex_channel ex_channel.cpp)
target_link_libraries(ex_channel glib-senders::glib-senders)

add_executable(ex_repeat_effect ex_repeat_effect.cpp)
target_link_libraries(ex_repeat_effect glib-senders::glib-senders)
# espp/format's bundled fmt reserves 500 ints on the stack for digit separators.
# Keep 32 inline; fmt's buffer still grows on demand for longer inputs.
idf_component_get_property(espp_format_dir espp__format COMPONENT_DIR)
set(fmt_headers_dir "${espp_format_dir}/detail/fmt/include/fmt")
set(fmt_format_header "${fmt_headers_dir}/format.h")
if(NOT EXISTS "${fmt_format_header}")
    return()
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${fmt_format_header}")
file(READ "${fmt_format_header}" fmt_original)
string(REPLACE "auto separators = basic_memory_buffer<int>();"
               "auto separators = basic_memory_buffer<int, 32>();"
               fmt_patched "${fmt_original}")
if(NOT fmt_original STREQUAL fmt_patched)
    # fmt headers include one another by relative path. Use the same generated
    # headers for all consumers of the header-only library.
    set(fmt_overlay "${CMAKE_CURRENT_BINARY_DIR}/ftp_fmt_include")
    file(GLOB fmt_peers CONFIGURE_DEPENDS "${fmt_headers_dir}/*.h")
    list(REMOVE_ITEM fmt_peers "${fmt_format_header}")
    foreach(peer IN LISTS fmt_peers)
        get_filename_component(name "${peer}" NAME)
        configure_file("${peer}" "${fmt_overlay}/fmt/${name}" COPYONLY)
    endforeach()
    file(CONFIGURE OUTPUT "${fmt_overlay}/fmt/format.h" CONTENT "${fmt_patched}" @ONLY)
    idf_component_get_property(espp_format_lib espp__format COMPONENT_LIB)
    target_include_directories(${espp_format_lib} BEFORE INTERFACE "${fmt_overlay}")
endif()

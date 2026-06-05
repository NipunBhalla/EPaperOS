# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# EPAPEROS_TASKS (passed via -DEPAPEROS_TASKS=ON in the env's cmake_extra_args)
# selects the full EPaperOS firmware: the Obsidian Tasks app under src/tasks/,
# which pulls in WiFi + HTTP. Without it we build a pure EPUB reader -- exclude
# src/tasks/ and drop the WiFi/HTTP components so they never link.
set(extra_reqs "")
if(DEFINED EPAPEROS_TASKS)
  set(extra_reqs esp_wifi esp_event esp_netif esp_http_client)
else()
  list(FILTER app_sources EXCLUDE REGEX "/src/tasks/")
endif()

# The main application component depends on epdiy (headers like <epdiy.h> and
# "epd_board.h" from components/epdiy/src) and on nvs_flash for persisted
# settings + reader prefs.
idf_component_register(SRCS ${app_sources}
                       REQUIRES epdiy nvs_flash ${extra_reqs})

# Drive the source-level #ifdef EPAPEROS_TASKS from the same CMake switch, so
# one flag controls both which files build and which code paths compile.
if(DEFINED EPAPEROS_TASKS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE EPAPEROS_TASKS)
endif()

# 1. The ULP app name must be unique (if multiple components use ULP).
set(ulp_app_name ulp_main)
#
# 2. Specify all assembly source files.
#    Files should be placed into a separate directory (in this case, ulp/),
#    which should not be added to COMPONENT_SRCS.
# set(ulp_s_sources "../ulp/main.S")
#
# 3. List all the component source files which include automatically
#    generated ULP export file, ${ulp_app_name}.h:
# set(ulp_exp_dep_srcs "main.cpp")
#
# 4. Call function to build ULP binary and embed in project using the argument
#    values above.
# ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")

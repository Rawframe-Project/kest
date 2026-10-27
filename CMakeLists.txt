# The one build of this tree. A preset in CMakePresets.json configures it into
# out/<preset>, and everything it makes lands there at the path the checks
# know it by: kest, kest-debug, libkest.a, examples/embed, tools/inward and
# the rest. docs/operations/development.md lists the presets and targets.
cmake_minimum_required(VERSION 3.25)

# The version is said once, in the public header.
file(STRINGS include/kest.h kest_version_line
     REGEX "^#define KEST_VERSION_STRING \"[0-9]+\\.[0-9]+\\.[0-9]+\"$")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" KEST_VERSION "${kest_version_line}")
if(NOT KEST_VERSION)
  message(FATAL_ERROR "include/kest.h does not say KEST_VERSION_STRING")
endif()
project(kest VERSION ${KEST_VERSION} LANGUAGES C)

include(CheckCCompilerFlag)

# The library is built the same way under every configuration: its own
# optimization flags below, and its internal assertions on, so a multi-config
# generator's Release adds nothing but what it is told.
foreach(config RELEASE RELWITHDEBINFO MINSIZEREL)
  string(REGEX REPLACE "[-/]DNDEBUG" "" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
endforeach()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# MSVC's static C runtime, which is what `cl` links when it is told nothing,
# so a host built with a plain `cl host.c kest.lib` links against the archive.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(MSVC)
  set(kest_sanitizers_default OFF)
else()
  set(kest_sanitizers_default ON)
endif()
option(KEST_WERROR "Treat warnings as errors" ON)
option(KEST_CHECKED "Build kest-debug and the sanitized hosts (AddressSanitizer and UBSan)"
       ${kest_sanitizers_default})
option(KEST_RACES "Build the library's objects for ThreadSanitizer" OFF)
option(KEST_FUZZ "Build the libFuzzer target tools/fuzz-cover (Clang only)" OFF)
option(KEST_WASM "Build kest.wasm and the playground (clang, wasm-ld and wasi-libc)" OFF)
set(KEST_RAYLIB "" CACHE PATH "raylib's src directory, built, for the tetris and colony hosts")
option(KEST_RELEASE "Install the sources, documentation and editor extension too, for a release archive" OFF)
if(WIN32)
  set(kest_lib_dir_default "lib/kest/")
else()
  set(kest_lib_dir_default "${CMAKE_INSTALL_PREFIX}/lib/kest/")
endif()
set(KEST_LIB_DIR "${kest_lib_dir_default}" CACHE STRING
    "Where an installed kest looks for the standard library last")

file(GLOB kest_sources CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM kest_sources ${PROJECT_SOURCE_DIR}/src/main.c)

# Warnings: the library is held to everything; the hosts drop shadowing and
# conversions, since each is one long main of blocks that reuse short names.
if(MSVC)
  set(kest_warn /W4 /experimental:c11atomics /D_CRT_SECURE_NO_WARNINGS)
  set(kest_host_warn /W4 /wd4456 /experimental:c11atomics /D_CRT_SECURE_NO_WARNINGS)
  set(kest_optimize /O2)
  if(KEST_WERROR)
    list(APPEND kest_warn /WX)
    list(APPEND kest_host_warn /WX)
  endif()
else()
  set(kest_warn -Wall -Wextra -Wshadow -Wconversion)
  set(kest_host_warn -Wall -Wextra)
  set(kest_optimize -O2)
  if(KEST_WERROR)
    list(APPEND kest_warn -Werror)
    list(APPEND kest_host_warn -Werror)
  endif()
endif()

# Every function and table in a section of its own, so a game linking with
# --gc-sections carries what it calls.
check_c_compiler_flag("-ffunction-sections -fdata-sections" kest_has_sections)
# The machine's dispatch loop is one switch; its cases are aligned, and its
# indirect jumps unmarked, where the compiler can be asked to.
check_c_compiler_flag(-falign-labels=16 kest_has_align_labels)
check_c_compiler_flag(-fcf-protection=return kest_has_cf_return)

set(kest_checked_flags -O0 -g -fsanitize=address,undefined)

# The build directory laid out the way the source tree is: the standard
# library in lib/std and the header in include beside the binaries, so a kest
# run from out/<preset> finds its library, and `kest build --release` finds
# kest.h above it and libkest.a beside it.
foreach(staged lib/std include)
  if(NOT EXISTS ${PROJECT_BINARY_DIR}/${staged})
    get_filename_component(above ${PROJECT_BINARY_DIR}/${staged} DIRECTORY)
    file(MAKE_DIRECTORY ${above})
    if(WIN32)
      file(COPY ${PROJECT_SOURCE_DIR}/${staged} DESTINATION ${above})
    else()
      file(CREATE_LINK ${PROJECT_SOURCE_DIR}/${staged}
           ${PROJECT_BINARY_DIR}/${staged} SYMBOLIC)
    endif()
  endif()
endforeach()

# Where a target lands: the same path under out/<preset> for every generator.
# The generator expression keeps a multi-config generator from adding a
# directory per configuration.
function(kest_output target directory)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<1:${PROJECT_BINARY_DIR}/${directory}>
    ARCHIVE_OUTPUT_DIRECTORY $<1:${PROJECT_BINARY_DIR}/${directory}>)
endfunction()

# The library and the command line.
add_library(libkest STATIC ${kest_sources})
set_target_properties(libkest PROPERTIES OUTPUT_NAME kest)
if(NOT MSVC)
  set_target_properties(libkest PROPERTIES PREFIX lib)
endif()
target_include_directories(libkest PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_definitions(libkest PRIVATE KEST_LIB_DIR="${KEST_LIB_DIR}")
target_compile_options(libkest PRIVATE ${kest_warn} ${kest_optimize})
if(kest_has_sections)
  target_compile_options(libkest PRIVATE -ffunction-sections -fdata-sections)
endif()
set(kest_vm_tuning "")
if(kest_has_align_labels)
  list(APPEND kest_vm_tuning -falign-labels=16)
endif()
if(kest_has_cf_return)
  list(APPEND kest_vm_tuning -fcf-protection=return)
endif()
if(kest_vm_tuning)
  set_source_files_properties(src/vm.c TARGET_DIRECTORY libkest PROPERTIES
    COMPILE_OPTIONS "${kest_vm_tuning}")
endif()
if(NOT WIN32)
  target_link_libraries(libkest PUBLIC m)
endif()
kest_output(libkest .)

add_library(kest_main OBJECT src/main.c)
target_include_directories(kest_main PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(kest_main PRIVATE KEST_LIB_DIR="${KEST_LIB_DIR}")
target_compile_options(kest_main PRIVATE ${kest_warn} ${kest_optimize})
add_executable(kest $<TARGET_OBJECTS:kest_main>)
target_link_libraries(kest PRIVATE libkest)
kest_output(kest .)

# A host compiled to an object of its own first, because what a host calls is
# readable in an object and gone once it is linked.
function(kest_host name source warn)
  add_library(${name}_object OBJECT ${source})
  target_compile_options(${name}_object PRIVATE ${warn} ${kest_optimize})
  target_include_directories(${name}_object PRIVATE ${PROJECT_SOURCE_DIR}/include)
  add_executable(${name} $<TARGET_OBJECTS:${name}_object>)
  target_link_libraries(${name} PRIVATE libkest)
  kest_output(${name} examples)
endfunction()
kest_host(least examples/least.c "${kest_warn}")
kest_host(embed examples/embed.c "${kest_host_warn}")
kest_host(engine examples/engine.c "${kest_host_warn}")

# Instruments: the inward crossing, the measuring host and the seeded fuzzer.
function(kest_tool name directory)
  add_executable(${name} ${ARGN})
  target_compile_options(${name} PRIVATE ${kest_warn} ${kest_optimize})
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(${name} PRIVATE libkest)
  kest_output(${name} ${directory})
endfunction()
kest_tool(inward tools tools/inward.c)
kest_tool(fuzz tools tools/fuzz.c)
kest_tool(measure bench bench/measure.c)

# The frame host, linked with what the C backend writes for the same program.
add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/build/frame-native.c
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/build
  COMMAND kest emit --c ${PROJECT_SOURCE_DIR}/bench/frame.kest
          > ${PROJECT_BINARY_DIR}/build/frame-native.c
  DEPENDS kest ${PROJECT_SOURCE_DIR}/bench/frame.kest
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_library(frame_native OBJECT ${PROJECT_BINARY_DIR}/build/frame-native.c)
target_compile_definitions(frame_native PRIVATE KEST_NO_MAIN)
target_include_directories(frame_native PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_options(frame_native PRIVATE ${kest_optimize})
kest_tool(frame bench bench/frame.c $<TARGET_OBJECTS:frame_native>)

# One number for each of four things a frame does, with the spread beside it:
# the frame step, a crossing out, a reference read and a crossing in. Not a
# test, because a duration is not a pass or a fail.
add_custom_target(time
  COMMAND kest run tools/frame.kest
  COMMAND kest run tools/crossing.kest
  COMMAND kest run tools/reference.kest
  COMMAND inward
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} USES_TERMINAL VERBATIM)

# The checking build: the library's own checks and AddressSanitizer and
# UndefinedBehaviorSanitizer, with the hosts and the fuzzer built against it.
if(KEST_CHECKED)
  add_library(kest_checked OBJECT ${kest_sources})
  target_include_directories(kest_checked PUBLIC ${PROJECT_SOURCE_DIR}/include)
  target_compile_definitions(kest_checked PRIVATE KEST_LIB_DIR="${KEST_LIB_DIR}")
  target_compile_options(kest_checked PRIVATE ${kest_warn} ${kest_checked_flags})

  function(kest_checked_executable name directory warn)
    add_executable(${name} ${ARGN} $<TARGET_OBJECTS:kest_checked>)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_definitions(${name} PRIVATE KEST_LIB_DIR="${KEST_LIB_DIR}")
    target_compile_options(${name} PRIVATE ${warn} ${kest_checked_flags})
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    target_link_libraries(${name} PRIVATE m)
    kest_output(${name} ${directory})
  endfunction()
  add_library(kest_checked_main OBJECT src/main.c)
  target_include_directories(kest_checked_main PRIVATE ${PROJECT_SOURCE_DIR}/include)
  target_compile_definitions(kest_checked_main PRIVATE KEST_LIB_DIR="${KEST_LIB_DIR}")
  target_compile_options(kest_checked_main PRIVATE ${kest_warn} ${kest_checked_flags})
  kest_checked_executable(kest-debug . "${kest_warn}" $<TARGET_OBJECTS:kest_checked_main>)
  # The library that checks itself as one archive, for a host the C backend
  # wrote to be linked against where a refused allocation can be aimed.
  add_library(kest_checked_archive STATIC $<TARGET_OBJECTS:kest_checked>)
  set_target_properties(kest_checked_archive PROPERTIES OUTPUT_NAME kest-checked)
  if(NOT MSVC)
    set_target_properties(kest_checked_archive PROPERTIES PREFIX lib)
  endif()
  kest_output(kest_checked_archive .)
  kest_checked_executable(embed-debug examples "${kest_host_warn}" examples/embed.c)
  kest_checked_executable(engine-debug examples "${kest_host_warn}" examples/engine.c)
  kest_checked_executable(fuzz-debug tools "${kest_host_warn}" tools/fuzz.c)
  target_include_directories(fuzz-debug PRIVATE ${PROJECT_SOURCE_DIR}/src)
endif()

# Every object of the release build and of the checking build, by the name of
# its source, where tools/check-dead.sh reads what each defines and calls:
# build/release and build/debug.
if(NOT MSVC)
  set(kest_release_objects
      $<TARGET_OBJECTS:libkest> $<TARGET_OBJECTS:kest_main> $<TARGET_OBJECTS:least_object>
      $<TARGET_OBJECTS:embed_object> $<TARGET_OBJECTS:engine_object>)
  add_custom_target(objects ALL
    COMMAND ${CMAKE_COMMAND} -DDEST=${PROJECT_BINARY_DIR}/build/release
            "-DOBJECTS=$<JOIN:${kest_release_objects},|>"
            -P ${PROJECT_SOURCE_DIR}/cmake/CopyObjects.cmake
    DEPENDS libkest kest_main least_object embed_object engine_object
    COMMENT "Objects of the release build into build/release" VERBATIM)
  if(KEST_CHECKED)
    add_custom_target(checked_objects ALL
      COMMAND ${CMAKE_COMMAND} -DDEST=${PROJECT_BINARY_DIR}/build/debug
              "-DOBJECTS=$<JOIN:$<TARGET_OBJECTS:kest_checked>;$<TARGET_OBJECTS:kest_checked_main>,|>"
              -P ${PROJECT_SOURCE_DIR}/cmake/CopyObjects.cmake
      DEPENDS kest_checked kest_checked_main
      COMMENT "Objects of the checking build into build/debug" VERBATIM)
  endif()
endif()

# The library's objects for ThreadSanitizer; the gate links its thread hosts
# against them (build/races/*.o).
if(KEST_RACES)
  add_library(kest_races OBJECT ${kest_sources})
  target_include_directories(kest_races PUBLIC ${PROJECT_SOURCE_DIR}/include)
  target_compile_definitions(kest_races PRIVATE KEST_LIB_DIR="${KEST_LIB_DIR}")
  target_compile_options(kest_races PRIVATE ${kest_warn} -O1 -g -fsanitize=thread)
  add_custom_target(races ALL
    COMMAND ${CMAKE_COMMAND} -DDEST=${PROJECT_BINARY_DIR}/build/races
            "-DOBJECTS=$<JOIN:$<TARGET_OBJECTS:kest_races>,|>"
            -P ${PROJECT_SOURCE_DIR}/cmake/CopyObjects.cmake
    DEPENDS kest_races COMMENT "ThreadSanitizer objects into build/races" VERBATIM)
endif()

# The libFuzzer target over the source boundary.
if(KEST_FUZZ)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "KEST_FUZZ needs clang's libFuzzer")
  endif()
  add_library(kest_cover OBJECT ${kest_sources})
  target_include_directories(kest_cover PUBLIC ${PROJECT_SOURCE_DIR}/include)
  target_compile_definitions(kest_cover PRIVATE KEST_LIB_DIR="${KEST_LIB_DIR}")
  target_compile_options(kest_cover PRIVATE -Wall -Wextra -O1 -g
                         -fsanitize=fuzzer-no-link,address,undefined)
  add_executable(fuzz-cover tools/fuzz-cover.c $<TARGET_OBJECTS:kest_cover>)
  target_include_directories(fuzz-cover PRIVATE ${PROJECT_SOURCE_DIR}/include)
  target_compile_options(fuzz-cover PRIVATE -Wall -Wextra -O1 -g
                         -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz-cover PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_libraries(fuzz-cover PRIVATE m)
  kest_output(fuzz-cover tools)
endif()

# The command line as WebAssembly, and the playground page around it. Built
# with clang for the target directly: one tree, two compilers.
if(KEST_WASM)
  find_program(KEST_WASM_CC NAMES clang REQUIRED)
  find_program(KEST_WASM_LD NAMES wasm-ld wasm-ld-20 wasm-ld-19 wasm-ld-18 REQUIRED)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(kest_wasm_objects "")
  foreach(source IN LISTS kest_sources ITEMS ${PROJECT_SOURCE_DIR}/src/main.c)
    get_filename_component(stem ${source} NAME_WE)
    set(object ${PROJECT_BINARY_DIR}/build/wasm/${stem}.o)
    add_custom_command(OUTPUT ${object}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/build/wasm
      COMMAND ${KEST_WASM_CC} --target=wasm32-wasi -std=c11 -O2 -Wall -Wextra -Werror
              -I${PROJECT_SOURCE_DIR}/include "-DKEST_LIB_DIR=\"/lib/kest/\""
              -c -o ${object} ${source}
      DEPENDS ${source} VERBATIM)
    list(APPEND kest_wasm_objects ${object})
  endforeach()
  add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/kest.wasm
    COMMAND ${KEST_WASM_CC} --target=wasm32-wasi -O2 -fuse-ld=${KEST_WASM_LD}
            -o ${PROJECT_BINARY_DIR}/kest.wasm ${kest_wasm_objects} -lm
    DEPENDS ${kest_wasm_objects} VERBATIM)
  set(kest_played chores records blocks vectors methods)
  add_custom_target(playground ALL
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${PROJECT_BINARY_DIR}/playground
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/playground
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/playground/index.html
            ${PROJECT_SOURCE_DIR}/playground/main.js ${PROJECT_SOURCE_DIR}/playground/wasi.js
            ${PROJECT_BINARY_DIR}/kest.wasm ${PROJECT_BINARY_DIR}/playground/
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/playground.py
            ${PROJECT_BINARY_DIR}/playground ${kest_played}
    DEPENDS ${PROJECT_BINARY_DIR}/kest.wasm
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} VERBATIM)
endif()

# The raylib hosts: Tetris and the colony, whose rules are also compiled to C.
if(KEST_RAYLIB)
  set(kest_raylib_libs ${KEST_RAYLIB}/libraylib.a m pthread dl rt GL X11)
  add_executable(tetris examples/tetris/host.c)
  set_target_properties(tetris PROPERTIES OUTPUT_NAME host)
  target_include_directories(tetris PRIVATE ${KEST_RAYLIB})
  target_compile_options(tetris PRIVATE ${kest_host_warn} ${kest_optimize})
  target_link_libraries(tetris PRIVATE libkest ${kest_raylib_libs})
  kest_output(tetris examples/tetris)

  file(GLOB colony_rules CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/examples/colony/game/*.kest)
  add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/build/colony/native.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_BINARY_DIR}/build/colony
    COMMAND kest emit --c examples/colony/game/colony.kest
            > ${PROJECT_BINARY_DIR}/build/colony/native.c
    DEPENDS kest ${colony_rules}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  add_executable(colony examples/colony/host/colony.c ${PROJECT_BINARY_DIR}/build/colony/native.c)
  set_source_files_properties(${PROJECT_BINARY_DIR}/build/colony/native.c PROPERTIES
    COMPILE_DEFINITIONS KEST_NO_MAIN)
  # The host is held to the hosts' warnings, as Tetris's is; the rules beside it
  # are what the C backend wrote, not a file anybody edits.
  set_source_files_properties(examples/colony/host/colony.c PROPERTIES
    COMPILE_OPTIONS "${kest_host_warn}")
  target_include_directories(colony PRIVATE ${KEST_RAYLIB})
  target_compile_options(colony PRIVATE ${kest_optimize})
  target_link_libraries(colony PRIVATE libkest ${kest_raylib_libs})
  kest_output(colony examples/colony)
endif()

# What a release archive says it was built for.
string(TOLOWER "${CMAKE_SYSTEM_NAME}" kest_system)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" kest_processor)
if(kest_processor STREQUAL "amd64")
  set(kest_processor x86_64)
elseif(kest_processor STREQUAL "arm64" AND NOT APPLE)
  set(kest_processor aarch64)
endif()

# Where another project looks: the command line, the header, the library and
# the standard library. A release archive adds the sources, the documentation
# and the editor extension, and says what it is in VERSION.
install(TARGETS kest RUNTIME DESTINATION bin)
install(TARGETS libkest ARCHIVE DESTINATION lib)
install(FILES include/kest.h DESTINATION include)
install(DIRECTORY lib/std DESTINATION lib/kest
        FILES_MATCHING PATTERN "*.kest")
if(KEST_RELEASE)
  install(DIRECTORY src/ DESTINATION src FILES_MATCHING PATTERN "*.[ch]")
  install(DIRECTORY editors/vscode DESTINATION editors)
  install(DIRECTORY docs DESTINATION .)
  install(FILES README.md CHANGELOG.md LICENSE DESTINATION .)
  install(CODE "
    execute_process(COMMAND \"\${CMAKE_INSTALL_PREFIX}/bin/kest${CMAKE_EXECUTABLE_SUFFIX}\" --version
                    OUTPUT_VARIABLE said OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    file(WRITE \"\${CMAKE_INSTALL_PREFIX}/VERSION\" \"\${said}
${kest_system}-${kest_processor}
unpack it anywhere; bin/kest finds lib/kest beside it
what it is and how to build a host against it: README.md
what changed: CHANGELOG.md. how to use it: docs/README.md
the licence every file of it is under: LICENSE
\")")
endif()

# A release archive: `cpack --preset <preset>` after a build with KEST_RELEASE.
set(CPACK_PACKAGE_FILE_NAME "kest-${KEST_VERSION}-${kest_system}-${kest_processor}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
if(WIN32)
  set(CPACK_GENERATOR ZIP)
else()
  set(CPACK_GENERATOR TGZ)
endif()
set(CPACK_PACKAGE_DIRECTORY ${PROJECT_BINARY_DIR}/package)
include(CPack)

enable_testing()
include(cmake/Tests.cmake)

cmake_minimum_required(VERSION 3.30.0)
project(arm-intc.library VERSION 0.1.0)

# "make install" gathers the products in build/install/: libs/ the library for LIBS:, sdk/ what a developer needs
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/build/install CACHE PATH "Install prefix" FORCE)
endif()

# devicetree.resource is used for its interface only (it is in the ROM of Emu68): its SFD gives proto/devicetree.h. Its sources are
# the submodule devicetree.resource/, or another copy with -DDEVICETREE_DIR=<path>.
set(DEVICETREE_DIR ${CMAKE_SOURCE_DIR}/devicetree.resource CACHE PATH "the sources of devicetree.resource")
if(NOT EXISTS ${DEVICETREE_DIR}/sfd/devicetree_lib.sfd)
    message(FATAL_ERROR "no ${DEVICETREE_DIR}/sfd/devicetree_lib.sfd: git submodule update --init, or -DDEVICETREE_DIR=<path>")
endif()

include(cmake/verstring.cmake)
include(cmake/sfdc.cmake)

get_verstring(VERSTRING)
sfdc(sfd/arm-intc_lib.sfd)
sfdc(${DEVICETREE_DIR}/sfd/devicetree_lib.sfd)

add_executable(arm-intc.library
    src/main.c
    src/init.c
    src/open.c
    src/close.c
    src/expunge.c
    src/controller.c
    src/dispatch.c
    src/tags.c
    src/aic_addintserver.c
    src/aic_remintserver.c
    src/aic_enableint.c
    src/aic_disableint.c
    src/aic_getinterrupt.c
    src/aic_queryinfotaglist.c
    src/end.c
)

# -fno-tree-loop-distribute-patterns: GCC may turn a loop that copies or clears into a call to memcpy() or memset(), and there is no C library
set(AIC_OPT "-Os" CACHE STRING "optimisation level of the library")
target_compile_options(arm-intc.library PRIVATE ${AIC_OPT} -m68040 -msmall-code -fomit-frame-pointer -Wall -Wno-pointer-sign -fno-tree-loop-distribute-patterns)
# One place for the version: project(), above. The id string, lib_Version and lib_Revision all come from it
target_compile_definitions(arm-intc.library PRIVATE
    VERSION_STRING="${VERSTRING}"
    AIC_VERSION=${PROJECT_VERSION_MAJOR}
    AIC_REVISION=${PROJECT_VERSION_MINOR}
)

# cmake -DAIC_DEBUG=ON: the library says what it does through kprintf() (RawPutChar: "rxnet debug" or Sashimi listen there)
option(AIC_DEBUG "debug messages of the library through kprintf()" OFF)
if(AIC_DEBUG)
    target_compile_definitions(arm-intc.library PRIVATE AIC_DEBUG)
endif()

target_include_directories(arm-intc.library PRIVATE include src)
target_link_options(arm-intc.library PRIVATE -ffreestanding -nostdlib -nostartfiles -s -Wl,-e,__start)
target_link_libraries(arm-intc.library devicetree arm_intc)

# ROM-able: one section, .text, no writable data (cmake/romable.cmake)
string(REGEX REPLACE "gcc$" "objdump" AIC_OBJDUMP ${CMAKE_C_COMPILER})
add_custom_command(TARGET arm-intc.library POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DOBJDUMP=${AIC_OBJDUMP} -DFILE=$<TARGET_FILE:arm-intc.library> -P ${CMAKE_SOURCE_DIR}/cmake/romable.cmake
    VERBATIM
)

# aic-probe: what the library relies on, proved on the machine without it. It links the library's code that reads the device tree
# (CLI, no C library; -O2: at -Os GCC may call memcpy() for the larger initialisers)
add_executable(aic-probe
    testcases/startup.c
    testcases/aic-probe.c
    src/controller.c
    src/aic_getinterrupt.c
)
target_compile_options(aic-probe PRIVATE -O2 -m68040 -fomit-frame-pointer -Wall -Wno-pointer-sign -Wno-main -fno-tree-loop-distribute-patterns)
target_include_directories(aic-probe PRIVATE include src)
target_link_options(aic-probe PRIVATE -ffreestanding -m68040 -nostdlib -nostartfiles -Wl,-e,__start)
target_link_libraries(aic-probe amiga devicetree arm_intc)

# aic-test: the library through its calls, on the machine (testcases/aic-run runs every test and keeps the results)
add_executable(aic-test
    testcases/startup.c
    testcases/aic-test.c
)
target_compile_options(aic-test PRIVATE -O2 -m68040 -fomit-frame-pointer -Wall -Wno-pointer-sign -Wno-main -fno-tree-loop-distribute-patterns)
target_compile_definitions(aic-test PRIVATE AIC_VERSION_EXPECTED=${PROJECT_VERSION_MAJOR} AIC_REVISION_EXPECTED=${PROJECT_VERSION_MINOR})
target_include_directories(aic-test PRIVATE include src)
target_link_options(aic-test PRIVATE -ffreestanding -m68040 -nostdlib -nostartfiles -Wl,-e,__start)
target_link_libraries(aic-test amiga devicetree arm_intc)

install(TARGETS arm-intc.library DESTINATION libs)
install(TARGETS aic-probe aic-test DESTINATION tools)
install(FILES testcases/aic-run DESTINATION tools)
install(FILES sfd/arm-intc_lib.sfd DESTINATION sdk/sfd)
install(FILES Autodocs/arm-intc.doc Autodocs/arm-intc.md DESTINATION sdk/Autodocs)
install(FILES include/libraries/arm-intc.h DESTINATION sdk/include/libraries)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/ DESTINATION sdk/include FILES_MATCHING PATTERN "*arm-intc*")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/FD/arm-intc.fd DESTINATION sdk/FD)

# wabar_kernel: helpers with no domain knowledge that two or more units use (proposal, section 6).
# Empty until migration step 5; an INTERFACE library until then so it carries only the include
# root and the Qt libraries every feature links. Step 5 turns it STATIC with its first sources.
add_library(wabar_kernel INTERFACE)
target_include_directories(wabar_kernel INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wabar_kernel INTERFACE Qt6::Core Qt6::Gui)

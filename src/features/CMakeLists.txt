# One line per feature. A feature may DEPENDS only on features listed above it (wabar_feature
# checks this at configure time). Deleting a feature is deleting its folder and its line here.
add_subdirectory(clock)
add_subdirectory(system_stats)
add_subdirectory(workspaces)

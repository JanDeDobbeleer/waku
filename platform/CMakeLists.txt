target_sources(yasb_core PRIVATE
  pdh.cpp
  memory.cpp
  battery.cpp
)

target_link_libraries(yasb_core PUBLIC pdh setupapi psapi)

set(YASB_SCHEMA_JSON "${CMAKE_SOURCE_DIR}/schema/schema.json")
set(YASB_SCHEMA_DATA_CPP "${CMAKE_CURRENT_BINARY_DIR}/schema_data.cpp")

# schema.json is compiled into the binary so the exe needs no data files next to it.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${YASB_SCHEMA_JSON}")
file(READ "${YASB_SCHEMA_JSON}" _schema_hex HEX)
string(LENGTH "${_schema_hex}" _schema_hex_len)
math(EXPR _schema_size "${_schema_hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _schema_bytes "${_schema_hex}")
file(CONFIGURE OUTPUT "${YASB_SCHEMA_DATA_CPP}" CONTENT
"#include <cstddef>
namespace yasb::config {
extern const unsigned char kEmbeddedSchema[];
extern const std::size_t kEmbeddedSchemaSize;
const unsigned char kEmbeddedSchema[] = {${_schema_bytes}0};
const std::size_t kEmbeddedSchemaSize = ${_schema_size};
}
" @ONLY)

# config: YAML loading, schema validation, paths and the generated typed options. yaml-cpp stays
# private; headers expose nlohmann::json and Qt Core (QLoggingCategory).
file(GLOB YASB_OPTIONS_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/options/*.h")
add_library(yasb_config STATIC
  deprecation.h
  deprecation.cpp
  loader.h
  loader.cpp
  paths.h
  paths.cpp
  schema_validator.h
  schema_validator.cpp
  ${YASB_OPTIONS_HEADERS}
  ${YASB_SCHEMA_DATA_CPP})
target_include_directories(yasb_config PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_config
  PUBLIC Qt6::Core nlohmann_json::nlohmann_json
  PRIVATE yasb_options yaml-cpp::yaml-cpp)

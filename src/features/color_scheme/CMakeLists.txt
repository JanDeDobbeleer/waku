# color_scheme: Material 3 dynamic colour with no external tool. A port of the core of
# material-color-utilities (HCT, tonal palettes, the nine scheme variants, the Material 3 role set,
# the Celebi quantizer and Score) plus the ColorScheme QML singleton that turns a colour or a
# wallpaper into a palette. No dependencies on other features.
waku_feature(NAME color_scheme
  SOURCES
    color_math.h
    color_math.cpp
    dynamic_scheme.h
    dynamic_scheme.cpp
    hct.h
    hct.cpp
    image_source.h
    image_source.cpp
    image_stats_cache.h
    image_stats_cache.cpp
    quantizer.h
    quantizer.cpp
    tonal_palette.h
    tonal_palette.cpp
  QML_SOURCES
    qml/color_scheme_service.h
    qml/color_scheme_service.cpp
    qml/color_scheme_types.h
  TESTS
    tests/test_color_scheme.cpp)

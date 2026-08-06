idf_component_register(SRCS "matter_endpoints.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_matter)

# Any TU that includes esp_matter.h needs CHIP's generated project config
# pulled in (and C++17) — same flags esp-matter's example apps set.
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

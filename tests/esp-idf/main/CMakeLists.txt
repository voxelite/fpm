idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "../../../include"
)

# The library requires C++26
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++26)

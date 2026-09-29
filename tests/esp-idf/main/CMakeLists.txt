idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "../../../include"
)

# The library requires C++26
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++26)

# The options of the library, like the ones of its own CMakeLists.txt: `idf.py -DFPM_DEFINED_OVERFLOW=ON build`
foreach(option FPM_DEFINED_OVERFLOW FPM_CHECK_OVERFLOW FPM_FRACTION_STRICT FPM_NO_FLOATING_WIDENING)
  if(${option})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${option})
  endif()
endforeach()

set(headers_list "")
# List of headers
list(APPEND headers_list jk_grad.h scf_grad.h )

# If you want to remove some headers specify them explictly here
if(DEVELOPMENT_CODE)
   list(REMOVE_ITEM headers_list "")
else()
   list(REMOVE_ITEM headers_list "")
endif()
# Sort alphabetically
list(SORT headers_list)

set(sources_list "")
# List of sources
list(APPEND sources_list response.cc jk_grad.cc scf_grad.cc wrapper.cc )

# If you want to remove some sources specify them explictly here
if(DEVELOPMENT_CODE)
   list(REMOVE_ITEM sources_list "")
else()
   list(REMOVE_ITEM sources_list "")
endif()


# Build static library
add_library(scfgrad STATIC ${sources_list})
# Specify dependencies for the library (if any)
add_dependencies(scfgrad mints)
set_property(GLOBAL APPEND PROPERTY PSILIB scfgrad)
if(BUILD_CUSTOM_BOOST)
   add_dependencies(scfgrad custom_boost)
endif()

# Sets install directory for all the headers in the list
install_list_FILES("${headers_list}" include/psi4/bin/scfgrad)

# Maintainer: joaander

include_directories(${HOOMD_PYTHON_INCLUDE_DIR})

################################
## Define common libraries used by every target in HOOMD

## An update to to CentOS5's python broke linking of the hoomd exe. According
## to an ancient post online, adding -lutil fixed this in python 2.2
set(ADDITIONAL_LIBS "")
if (UNIX AND NOT APPLE)
    find_library(UTIL_LIB util /usr/lib)
    find_library(DL_LIB dl /usr/lib)
    set(ADDITIONAL_LIBS ${UTIL_LIB} ${DL_LIB})
    if (DL_LIB AND UTIL_LIB)
    mark_as_advanced(UTIL_LIB DL_LIB)
    endif (DL_LIB AND UTIL_LIB)
endif (UNIX AND NOT APPLE)

set(HOOMD_COMMON_LIBS
        ${HOOMD_PYTHON_LIBRARY}
        ${ADDITIONAL_LIBS}
        )

if (ENABLE_CUDA)
    if (NOT CUSOLVER_AVAILABLE)
    list(APPEND HOOMD_COMMON_LIBS ${CUDA_LIBRARIES} ${CUDA_cufft_LIBRARY} ${CUDA_curand_LIBRARY})
    else()
    list(APPEND HOOMD_COMMON_LIBS ${CUDA_LIBRARIES} ${CUDA_cufft_LIBRARY} ${CUDA_curand_LIBRARY} ${CUDA_cusolver_LIBRARY} ${CUDA_cusparse_LIBRARY})
    endif()

    if (ENABLE_NVTOOLS)
        list(APPEND HOOMD_COMMON_LIBS ${CUDA_nvToolsExt_LIBRARY})
    endif()
endif (ENABLE_CUDA)

if (ENABLE_MPI)
    list(APPEND HOOMD_COMMON_LIBS ${MPI_CXX_LIBRARIES})
endif (ENABLE_MPI)

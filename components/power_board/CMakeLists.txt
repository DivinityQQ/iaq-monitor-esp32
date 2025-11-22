set(PF_ROOT ${CMAKE_CURRENT_LIST_DIR}/powerfeather)
set(PF_SRCS
    ${PF_ROOT}/Mainboard/Mainboard.cpp
    ${PF_ROOT}/Mainboard/LC709204F.cpp
    ${PF_ROOT}/Mainboard/BQ2562x.cpp
    ${PF_ROOT}/Utils/MasterI2C.cpp
    ${PF_ROOT}/Utils/Mutex.cpp
)

idf_component_register(
    SRCS
        "power_board.cpp"
        ${PF_SRCS}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        ${PF_ROOT}
    PRIV_REQUIRES
        driver
        iaq_data
        iaq_profiler
        system_context
)

# Enable C++ for this component; IDF will infer from .cpp, but this keeps intent clear.
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

file(GLOB_RECURSE NESTED_SRC
       **.cpp
)
# Exclude any clock.cpp found in other directories
list(FILTER NESTED_SRC EXCLUDE REGEX ".*/clock.cpp$")

#if build variant is "devel", set to "fibonacci"
if (BUILD_VARIANT STREQUAL "devel")
    set(BUILD_VARIANT "nixie")
endif()

list(APPEND INCLUDE_DIRS ".")
list(APPEND INCLUDE_DIRS "internet_time")
list(APPEND INCLUDE_DIRS "led")
list(APPEND INCLUDE_DIRS "api")
list(APPEND INCLUDE_DIRS "${BUILD_VARIANT}")

# Explicitly include clock.cpp only from the selected BUILD_VARIANT folder
list(APPEND NESTED_SRC "${CMAKE_CURRENT_SOURCE_DIR}/${BUILD_VARIANT}/clock.cpp")

message(STATUS ${NESTED_SRC})

idf_component_register(
    SRCS ${NESTED_SRC}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    REQUIRES lwip esp_wifi heap json bootloader_support kd_common esp_http_client wifi_provisioning esp_driver_rmt mbedtls driver esp_http_server
)

idf_build_set_property(COMPILE_OPTIONS "-Wno-missing-field-initializers" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=parentheses" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-write-strings" APPEND)

idf_build_set_property(COMPILE_OPTIONS "-DDEVICE_NAME_PREFIX=\"CLOCK\"" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DKD_COMMON_CRYPTO_DISABLE" APPEND)
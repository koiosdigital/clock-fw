# clock-app component CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Set component name
set(COMPONENT_NAME "clock-app")

# Define the GitHub repository details
set(GITHUB_REPO "koiosdigital/clock-app")
set(STATIC_FILES_HEADER "static_files.h")
set(DOWNLOAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/downloads")
set(INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include")
set(STATIC_FILES_PATH "${INCLUDE_DIR}/${STATIC_FILES_HEADER}")

# Create directories
file(MAKE_DIRECTORY ${DOWNLOAD_DIR})
file(MAKE_DIRECTORY ${INCLUDE_DIR})

# Function to get the latest release tag
function(get_latest_release_tag REPO_NAME OUTPUT_VAR)
    execute_process(
        COMMAND curl -s "https://api.github.com/repos/${REPO_NAME}/releases/latest"
        COMMAND grep "tag_name"
        COMMAND head -n 1
        COMMAND cut -d ":" -f 2
        COMMAND sed "s/[\", ]//g"
        OUTPUT_VARIABLE LATEST_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE CURL_RESULT
    )
    
    if(CURL_RESULT EQUAL 0 AND NOT "${LATEST_TAG}" STREQUAL "")
        set(${OUTPUT_VAR} ${LATEST_TAG} PARENT_SCOPE)
        message(STATUS "Latest release tag for ${REPO_NAME}: ${LATEST_TAG}")
    else()
        set(${OUTPUT_VAR} "main" PARENT_SCOPE)
        message(WARNING "Failed to get latest release tag for ${REPO_NAME}, using 'main' branch")
    endif()
endfunction()

# Register the component
idf_component_register(
    INCLUDE_DIRS ${INCLUDE_DIR}
    REQUIRES esp_http_server
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)

# Get the latest release tag
get_latest_release_tag(${GITHUB_REPO} LATEST_TAG)

# Download URL for the static_files.h
set(DOWNLOAD_URL "https://github.com/${GITHUB_REPO}/releases/download/${LATEST_TAG}/${STATIC_FILES_HEADER}")

# Custom command to download the static_files.h
add_custom_command(
    OUTPUT ${STATIC_FILES_PATH}
    COMMAND curl -L -f -o ${STATIC_FILES_PATH} ${DOWNLOAD_URL}
    COMMENT "Downloading ${STATIC_FILES_HEADER} from ${GITHUB_REPO} (${LATEST_TAG})"
    VERBATIM
)

# Create a custom target to ensure download happens
add_custom_target(download_static_files
    DEPENDS ${STATIC_FILES_PATH}
)

# Add dependency to ensure download happens before component is built
add_dependencies(${COMPONENT_LIB} download_static_files)

# Add a fallback static_files.h if download fails
set(FALLBACK_CONTENT "#pragma once
// Fallback static_files.h when download fails
namespace static_files {
    struct file {
        const char* path;
        const unsigned char* contents;
        size_t size;
    };
    
    // Empty files array as fallback
    const file files[] = {};
    const int num_of_files = 0;
}
")

# Create fallback file if the downloaded file doesn't exist
add_custom_command(
    TARGET download_static_files
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Checking if ${STATIC_FILES_PATH} exists..."
    COMMAND test -f ${STATIC_FILES_PATH} || echo "${FALLBACK_CONTENT}" > ${STATIC_FILES_PATH}
    COMMENT "Creating fallback static_files.h if download failed"
    VERBATIM
)

# Print status information
message(STATUS "Clock-app component configured:")
message(STATUS "  Repository: ${GITHUB_REPO}")
message(STATUS "  Target file: ${STATIC_FILES_HEADER}")
message(STATUS "  Download URL: ${DOWNLOAD_URL}")
message(STATUS "  Local path: ${STATIC_FILES_PATH}")

endif()
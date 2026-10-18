include(FetchContent)

FetchContent_Declare(adecode
    GIT_REPOSITORY "https://github.com/alexbatalov/adecode"
    GIT_TAG e4a8b0f3b66826e0b7779a25d79df8497f8f8087 # v1.0.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    # ACM band format 31 (speech in some Russian localizations), as Fallout 2
    # CE has it.
    PATCH_COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_LIST_DIR}/fmt31.cmake"
)

FetchContent_MakeAvailable(adecode)

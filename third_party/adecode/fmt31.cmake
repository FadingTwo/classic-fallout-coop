# Run in adecode's source folder (FetchContent's PATCH_COMMAND): ACM band
# format 31, as Fallout 2 CE has it (its #246: speech missing in some
# Russian localizations). The block's samples come as they are, 16 bits
# each, already decoded and transformed. Other unknown formats still end
# the sound, as before. Does nothing when already patched.

set(file "${CMAKE_CURRENT_SOURCE_DIR}/src/adecode.c")
if(NOT EXISTS "${file}")
    set(file "src/adecode.c")
endif()
file(READ "${file}" text)

string(FIND "${text}" "ReadBand_Fmt31" done)
if(NOT done EQUAL -1)
    return()
endif()

function(replace_once old new)
    string(FIND "${text}" "${old}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "adecode fmt31 patch: not found:\n${old}")
    endif()
    string(REPLACE "${old}" "${new}" text "${text}")
    set(text "${text}" PARENT_SCOPE)
endfunction()

replace_once("static int ReadBands(AudioDecoder* ad);\n"
    "static int ReadBands(AudioDecoder* ad);\nstatic int ReadBand_Fmt31(AudioDecoder* ad, int subband, int n);\n")

replace_once("    ReadBand_Fmt29,\n    ReadBand_Fail,\n    ReadBand_Fail,\n};"
    "    ReadBand_Fmt29,\n    ReadBand_Fail,\n    ReadBand_Fmt31,\n};")

# A band's reader: 1 read, 0 failed, -1 the whole block read at once.
replace_once("        if (!ReadBand_tbl[n](ad, i, n)) {\n            return 0;\n        }\n    }\n    return 1;"
    "        int rc = ReadBand_tbl[n](ad, i, n);\n        if (rc <= 0) {\n            return rc;\n        }\n    }\n    return 1;")

replace_once("    if (!ReadBands(ad)) {\n        return 0;\n    }\n\n    untransform_all(ad);"
    "    int rc = ReadBands(ad);\n    if (rc == 0) {\n        return 0;\n    }\n\n    /* Format 31 comes transformed. */\n    if (rc > 0) {\n        untransform_all(ad);\n    }")

string(APPEND text "
/* Format 31: the whole block's samples as they are, 16 bits each (some
   Russian localizations; as Fallout 2 CE). */
static int ReadBand_Fmt31(AudioDecoder* ad, int subband, int n)
{
    int* samples = ad->samples;
    int remaining = ad->total_samples;

    (void)subband;
    (void)n;

    while (remaining != 0) {
        int value;

        requireBits(ad, 16);
        value = (int16_t)takeBits(ad, 16);
        dropBits(ad, 16);

        *samples++ = value * (1 << ad->levels);
        remaining--;
    }

    return -1;
}
")

file(WRITE "${file}" "${text}")

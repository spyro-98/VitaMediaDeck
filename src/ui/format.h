#ifndef VITAMEDIADECK_UI_FORMAT_H
#define VITAMEDIADECK_UI_FORMAT_H

#include <stddef.h>
#include <stdint.h>

/* Formats storage without collapsing small files to "0 MB". */
void ui_format_file_size(uint64_t bytes, char *out, size_t out_size);

#endif

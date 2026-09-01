#include "ui/format.h"

#include <stdio.h>

void ui_format_file_size(uint64_t bytes, char *out, size_t out_size) {
	if (!out || !out_size) return;
	if (bytes < 1024ULL) {
		snprintf(out, out_size, "%llu B", (unsigned long long)bytes);
	} else if (bytes < 1024ULL * 1024ULL) {
		snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
	} else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
		snprintf(out, out_size, "%.1f MB",
		         (double)bytes / (1024.0 * 1024.0));
	} else {
		snprintf(out, out_size, "%.2f GB",
		         (double)bytes / (1024.0 * 1024.0 * 1024.0));
	}
}

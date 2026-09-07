/* vita-elf-create appends module/import metadata to the executable segment.
 * A page-aligned data segment can leave less than the required ~4 KiB gap.
 * Reserve an unmapped 64 KiB address gap, not a loadable padding section, so
 * conversion remains valid regardless of small code-size changes with LTO.
 */
SECTIONS
{
  . = ALIGN(0x10000) + 0x10000;
}
INSERT BEFORE .preinit_array;

/*
 * The display the boot loader gave (1.2 stage 5); see display.h.
 */
#include "kernel/display/display.h"
#include "kernel/kprintf.h"
#include "kernel/memory/vm_layout.h"

static struct fb_info info;

void display_probe(uint32_t mb_info)
{
    struct fbinfo_raw raw;
    char line[FBINFO_LINE];
    /* The boot tables still map 0-4 GiB in the physical view. */
    fbinfo_read(phys_view(mb_info), &raw);
    fbinfo_parse(&raw, &info, line);
    klogf("%s\n", line);
}

const struct fb_info *display_info(void)
{
    return &info;
}

#include "mem.h"
#include "klog.h"

#define MB_MAGIC 0x2BADB002u

struct mb_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed));

struct mb_mmap {
    uint32_t size;          /* size of the rest of this entry */
    uint64_t base;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

static uint32_t usable_kb = 0;

static const char *type_name(uint32_t t)
{
    switch (t) {
    case 1:  return "usable";
    case 2:  return "reserved";
    case 3:  return "ACPI data";
    case 4:  return "ACPI NVS";
    case 5:  return "bad RAM";
    default: return "unknown";
    }
}

uint32_t mem_usable_kb(void) { return usable_kb; }

void mem_init(uint32_t magic, uint32_t mbi)
{
    const struct mb_info *info;
    uint64_t total = 0;
    uint32_t off;

    if (magic != MB_MAGIC || mbi == 0) {
		usable_kb = 0;
        kwarn("mem", "no Multiboot memory map; using unknown memory size", magic);
        return;
    }
	
	info = (const struct mb_info *)mbi;

    if (info->flags & 1) {
        kinfo("mem", "BIOS: %u KB lower, %u KB upper", info->mem_lower, info->mem_upper);
        usable_kb = info->mem_lower + info->mem_upper;
    }

    if (!(info->flags & (1u << 6))) {
        kwarn("mem", "loader gave no memory map");
        return;
    }

    for (off = 0; off + 24 <= info->mmap_length; ) {
        const struct mb_mmap *e = (const struct mb_mmap *)(info->mmap_addr + off);
        uint64_t base = e->base;
        uint64_t len  = e->len;
        uint64_t end  = base + len - 1;
        uint32_t type = e->type;
        uint32_t size = e->size;

        kinfo("mem", "%08x%08x-%08x%08x %s (%u KB)",
              (uint32_t)(base >> 32), (uint32_t)base,
              (uint32_t)(end >> 32),  (uint32_t)end,
              type_name(type), (uint32_t)(len >> 10));

        if (type == 1) total += len;
        if (size < 20) break;               /* corrupt entry, don't loop forever */
        off += size + 4;
    }

    usable_kb = (uint32_t)(total >> 10);
    kok("mem", "%u KB (%u MB) usable", usable_kb, usable_kb >> 10);
}
#ifndef INSTALL_H
#define INSTALL_H

#include <stdint.h>

#define INST_MAX_DISKS 4

struct inst_disk {
    int ch, sl;              /* ATA channel (0 primary, 1 secondary) and slave flag */
    uint32_t sectors;
    const char *name;        /* "Primary master" ... */
    const char *note;        /* NULL if the disk can be used, else why not */
    int usable;
};

enum {
    INST_OK        =  0,
    INST_E_PAYLOAD = -1,     /* no boot image to copy from */
    INST_E_SIZE    = -2,     /* disk too small */
    INST_E_BUSY    = -3,     /* disk did not answer */
    INST_E_WRITE   = -4,
    INST_E_FORMAT  = -5,
    INST_E_VERIFY  = -6
};

/* Finds plain ATA disks on both channels. Returns how many were filled in. */
int install_probe(struct inst_disk *d, int max);

/* Installs to d. progress(step, pct): step 0..4, pct 0..100. Returns INST_OK or INST_E_*. */
int install_run(const struct inst_disk *d, void (*progress)(int step, int pct));

const char *install_error(int e);

#endif
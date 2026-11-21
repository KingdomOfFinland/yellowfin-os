/*
 * e1000.c - Intel 82540EM network driver
 * YellowfinOS
 */

#include "e1000.h"
#include "klog.h"

/* Register offsets */

#define REG_CTRL       0x0000
#define REG_STATUS     0x0008
#define REG_IMC        0x00D8
#define REG_ICR        0x00C0

#define REG_RCTL       0x0100
#define REG_TCTL       0x0400
#define REG_TIPG       0x0410

#define REG_RDBAL      0x2800
#define REG_RDBAH      0x2804
#define REG_RDLEN      0x2808
#define REG_RDH        0x2810
#define REG_RDT        0x2818

#define REG_TDBAL      0x3800
#define REG_TDBAH      0x3804
#define REG_TDLEN      0x3808
#define REG_TDH        0x3810
#define REG_TDT        0x3818

#define E1000_CTRL_RST       (1u << 26)

#define E1000_RCTL_EN        (1u << 1)
#define E1000_RCTL_BAM       (1u << 15)
#define E1000_RCTL_SECRC     (1u << 26)

#define E1000_TCTL_EN        (1u << 1)
#define E1000_TCTL_PSP       (1u << 3)

#define E1000_TX_CMD_EOP     (1u << 0)
#define E1000_TX_CMD_IFCS    (1u << 1)
#define E1000_TX_CMD_RS      (1u << 3)

#define E1000_TX_STATUS_DD   (1u << 0)
#define E1000_RX_STATUS_DD   (1u << 0)
#define E1000_RX_STATUS_EOP (1u << 1)

#define E1000_RING_COUNT     128
#define E1000_BUFFER_SIZE    2048
#define E1000_MAX_FRAME      1518

/*
 * Hardware descriptor layouts.
 * Each descriptor is exactly 16 bytes.
 */

struct e1000_rx_desc {
    u32 address_low;
    u32 address_high;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
};

struct e1000_tx_desc {
    u32 address_low;
    u32 address_high;
    u16 length;
    u8 checksum_offset;
    u8 command;
    u8 status;
    u8 checksum_start;
    u16 special;
};

/* DMA memory */

static volatile struct e1000_rx_desc rx_ring[E1000_RING_COUNT]
    __attribute__((aligned(16)));

static volatile struct e1000_tx_desc tx_ring[E1000_RING_COUNT]
    __attribute__((aligned(16)));

static u8 rx_buffers[E1000_RING_COUNT][E1000_BUFFER_SIZE]
    __attribute__((aligned(16)));

static u8 tx_buffers[E1000_RING_COUNT][E1000_BUFFER_SIZE]
    __attribute__((aligned(16)));

/* Driver state */

static u32 e1000_mmio;
static u8 rx_next;
static u8 tx_next;
static int e1000_ready;

/* MMIO access */

static u32 e1000_read_reg(u32 reg)
{
    volatile u32 *address;

    address = (volatile u32 *)(e1000_mmio + reg);

    return *address;
}

static void e1000_write_reg(u32 reg, u32 value)
{
    volatile u32 *address;

    address = (volatile u32 *)(e1000_mmio + reg);

    *address = value;

    __asm__ volatile ("" ::: "memory");
}

static void e1000_read_mac(u8 mac[6])
{
    u32 ral;
    u32 rah;
    int i;

    ral = e1000_read_reg(0x5400);
    rah = e1000_read_reg(0x5404);

    for (i = 0; i < 4; i++)
        mac[i] = (u8)(ral >> (i * 8));

    for (i = 0; i < 2; i++)
        mac[i + 4] = (u8)(rah >> (i * 8));
}

/* PCI detection */

int e1000_detect(struct pci_device *out)
{
    u32 bar0;
    u32 bar_type;
    u32 status;
    u32 speed;
    u8 mac[6];

    if (!out)
        return 0;

    if (!pci_find_device(0x8086, 0x100E, out)) {
        kwarn("e1000", "Intel 82540EM not found", 0);
        return 0;
    }

    kinfo("e1000", "Intel 82540EM detected");

    kinfo("e1000", "PCI location %x:%x.%x",
          out->bus, out->slot, out->function);

    bar0 = pci_read_config(
        out->bus, out->slot, out->function, 0x10
    );

    kinfo("e1000", "BAR0 raw value: %08x", bar0);

    if (bar0 == 0 || bar0 == 0xFFFFFFFFu)
        return 0;

    if (bar0 & 1u) {
        kwarn("e1000", "BAR0 uses I/O space", bar0);
        return 0;
    }

    bar_type = (bar0 >> 1) & 3u;

    if (bar_type != 0) {
        kwarn("e1000", "Unsupported BAR type", bar_type);
        return 0;
    }

    e1000_mmio = bar0 & 0xFFFFFFF0u;

    kinfo("e1000", "BAR0 is a 32-bit memory BAR");
    kinfo("e1000", "MMIO physical base: %08x", e1000_mmio);

    status = e1000_read_reg(REG_STATUS);

    kinfo("e1000", "STATUS register: %08x", status);

    kinfo("e1000", "Link: %s",
          (status & (1u << 1)) ? "UP" : "DOWN");

    e1000_read_mac(mac);

    kinfo("e1000",
          "MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
          mac[0], mac[1], mac[2],
          mac[3], mac[4], mac[5]);

    speed = (status >> 6) & 3u;

    switch (speed) {
    case 0:
        kinfo("e1000", "Link speed: 10 Mbps");
        break;

    case 1:
        kinfo("e1000", "Link speed: 100 Mbps");
        break;

    case 2:
        kinfo("e1000", "Link speed: 1000 Mbps");
        break;

    default:
        kwarn("e1000", "Unknown link speed", speed);
        break;
    }

    kinfo("e1000", "Duplex: %s",
          (status & 1u) ? "Full" : "Half");

    return 1;
}


void e1000_rx_debug(void)
{
    u32 rdh = e1000_read_reg(REG_RDH);
    u32 rdt = e1000_read_reg(REG_RDT);
    u32 rctl = e1000_read_reg(REG_RCTL);
    u32 status = e1000_read_reg(REG_STATUS);

    kinfo("e1000", "RX head=%u tail=%u", rdh, rdt);
    kinfo("e1000", "RCTL=%08x STATUS=%08x", rctl, status);

    kinfo("e1000", "next descriptor status=%02x length=%u errors=%02x",
          rx_ring[rx_next].status,
          rx_ring[rx_next].length,
          rx_ring[rx_next].errors);
}


/* Hardware initialization */

int e1000_init(struct pci_device *dev)
{
    u32 bar0;
    u32 ctrl;
    u32 i;

    if (!dev)
        return 0;

    e1000_ready = 0;

    bar0 = pci_read_config(
        dev->bus, dev->slot, dev->function, 0x10
    );

    if ((bar0 == 0) || (bar0 == 0xFFFFFFFFu) || (bar0 & 1u))
        return 0;

    e1000_mmio = bar0 & 0xFFFFFFF0u;

    /* Enable MMIO decoding, but not bus mastering yet. */
    pci_enable(dev->bus, dev->slot, dev->function, 0x02);

    /* Disable device interrupts; this driver polls for now. */
    e1000_write_reg(REG_IMC, 0xFFFFFFFFu);
    (void)e1000_read_reg(REG_ICR);

    /* Reset the controller. */
    ctrl = e1000_read_reg(REG_CTRL);
    e1000_write_reg(REG_CTRL, ctrl | E1000_CTRL_RST);

    for (i = 0; i < 10000000u; i++) {
        if (!(e1000_read_reg(REG_CTRL) & E1000_CTRL_RST))
            break;
    }

    if (i == 10000000u) {
        kwarn("e1000", "Hardware reset timed out", 0);
        return 0;
    }

    kok("e1000", "hardware reset complete");

    /* Disable interrupts again after reset. */
    e1000_write_reg(REG_IMC, 0xFFFFFFFFu);
    (void)e1000_read_reg(REG_ICR);

    /*
     * Initialize RX descriptors.
     * Every descriptor points to its own 2048-byte buffer.
     */

    for (i = 0; i < E1000_RING_COUNT; i++) {
        u32 address = (u32)&rx_buffers[i][0];

        rx_ring[i].address_low = address;
        rx_ring[i].address_high = 0;
        rx_ring[i].length = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status = 0;
        rx_ring[i].errors = 0;
        rx_ring[i].special = 0;
    }

    rx_next = 0;

    e1000_write_reg(REG_RDBAL, (u32)&rx_ring[0]);
    e1000_write_reg(REG_RDBAH, 0);
    e1000_write_reg(REG_RDLEN,
                    sizeof(struct e1000_rx_desc) * E1000_RING_COUNT);
    e1000_write_reg(REG_RDH, 0);
    e1000_write_reg(REG_RDT, E1000_RING_COUNT - 1);

    /* Receive enable, broadcast accept, strip Ethernet CRC. */
    e1000_write_reg(REG_RCTL,
                    E1000_RCTL_EN |
                    E1000_RCTL_BAM |
                    E1000_RCTL_SECRC);

    kok("e1000", "RX ring initialized");

    /*
     * Initialize TX descriptors.
     */

    for (i = 0; i < E1000_RING_COUNT; i++) {
        u32 address = (u32)&tx_buffers[i][0];

        tx_ring[i].address_low = address;
        tx_ring[i].address_high = 0;
        tx_ring[i].length = 0;
        tx_ring[i].checksum_offset = 0;
        tx_ring[i].command = 0;
        tx_ring[i].status = E1000_TX_STATUS_DD;
        tx_ring[i].checksum_start = 0;
        tx_ring[i].special = 0;
    }

    tx_next = 0;

    e1000_write_reg(REG_TDBAL, (u32)&tx_ring[0]);
    e1000_write_reg(REG_TDBAH, 0);
    e1000_write_reg(REG_TDLEN,
                    sizeof(struct e1000_tx_desc) * E1000_RING_COUNT);
    e1000_write_reg(REG_TDH, 0);
    e1000_write_reg(REG_TDT, 0);

    /* Enable transmitter and pad short frames. */
    e1000_write_reg(REG_TCTL,
                    E1000_TCTL_EN |
                    E1000_TCTL_PSP |
                    (0x0Fu << 4) |
                    (0x40u << 12));

    e1000_write_reg(REG_TIPG, 0x0060200Au);

    kok("e1000", "TX ring initialized");

    /*
     * DMA addresses are now configured.
     * Enable PCI memory decoding and bus mastering.
     */

    pci_enable(dev->bus, dev->slot, dev->function, 0x06);

    e1000_ready = 1;

    kok("e1000", "bus mastering enabled");
    kok("e1000", "network interface ready");

    return 1;
}

/* Transmit one Ethernet frame */

int e1000_send(const void *data, u16 length)
{
    volatile struct e1000_tx_desc *desc;
    const u8 *source;
    u8 *destination;
    u32 i;
    u32 wait;
    u16 send_length;

    if (!e1000_ready || !data)
        return 0;

    if (length < 14 || length > E1000_MAX_FRAME)
        return 0;

    desc = &tx_ring[tx_next];

    /* Wait until hardware has finished with this descriptor. */
    for (wait = 0; wait < 10000000u; wait++) {
        if (desc->status & E1000_TX_STATUS_DD)
            break;
    }

    if (wait == 10000000u)
        return 0;

    source = (const u8 *)data;
    destination = tx_buffers[tx_next];

    for (i = 0; i < length; i++)
        destination[i] = source[i];

    /* Ethernet frames must be at least 60 bytes without the FCS. */
    send_length = length < 60 ? 60 : length;

    for (i = length; i < send_length; i++)
        destination[i] = 0;

    desc->address_low = (u32)destination;
    desc->address_high = 0;
    desc->length = send_length;
    desc->checksum_offset = 0;
    desc->command = E1000_TX_CMD_EOP |
                    E1000_TX_CMD_IFCS |
                    E1000_TX_CMD_RS;
    desc->status = 0;
    desc->checksum_start = 0;
    desc->special = 0;

    __asm__ volatile ("" ::: "memory");

    e1000_write_reg(REG_TDT,
                    (tx_next + 1) % E1000_RING_COUNT);

    /* Wait for hardware completion. */
    for (wait = 0; wait < 10000000u; wait++) {
        if (desc->status & E1000_TX_STATUS_DD)
            break;
    }

    if (wait == 10000000u)
        return 0;

    tx_next = (tx_next + 1) % E1000_RING_COUNT;

    return 1;
}


/* Receive one Ethernet frame.
 * Returns copied byte count, or zero if no valid frame is ready.
 */

int e1000_receive(void *buffer, u16 capacity)
{
    volatile struct e1000_rx_desc *desc;
    u8 *destination;
    u8 *source;
    u16 length;
    u16 copied = 0;
    u8 status;
    u8 errors;
    u32 i;

    if (!e1000_ready || !buffer || capacity == 0)
        return 0;

    desc = &rx_ring[rx_next];

    /* Ensure descriptor status is read before packet data. */
    __asm__ volatile ("" ::: "memory");

    status = desc->status;

    if (!(status & E1000_RX_STATUS_DD))
        return 0;

    errors = desc->errors;
    length = desc->length;

    /* Reject incomplete or corrupted frames. */
    if (!(status & (1u << 1)) ||
        errors != 0 ||
        length < 14 ||
        length > E1000_MAX_FRAME ||
        length > E1000_BUFFER_SIZE) {

        goto recycle;
    }

    copied = length;

    if (copied > capacity)
        copied = capacity;

    source = rx_buffers[rx_next];
    destination = (u8 *)buffer;

    for (i = 0; i < copied; i++)
        destination[i] = source[i];

recycle:

    /* Clear descriptor for hardware reuse. */
    desc->length = 0;
    desc->checksum = 0;
    desc->status = 0;
    desc->errors = 0;
    desc->special = 0;

    __asm__ volatile ("" ::: "memory");

    /* Return this descriptor to the RX hardware. */
    e1000_write_reg(REG_RDT, rx_next);

    rx_next = (rx_next + 1) % E1000_RING_COUNT;

    if (errors != 0 ||
        length < 14 ||
        length > E1000_MAX_FRAME ||
        length > E1000_BUFFER_SIZE ||
        !(status & (1u << 1)))
        return 0;

    return copied;
}

int e1000_is_ready(void)
{
    return e1000_ready;
}

int e1000_get_mac(u8 mac[6])
{
    if (!e1000_ready || !mac)
        return 0;

    e1000_read_mac(mac);
    return 1;
}
#include "ftfs.h"
#include "timer.h"
#include "rtc.h"
#include "e1000.h"
#include "ata.h"
#include "klog.h"
#include "panic.h"
#include "mem.h"
#include "ui.h"
#include "graphics.h"
#include "mouse.h"
#include "deskt.h"
#include "glass.h"
#include "keys.h"
#include "pci.h"
#include "arp.h"
#include "ethernet.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "dns.h"
#include "tcp.h"
#include "http.h"

#define VGA_W 80
#define VGA_H 25
#define MAXLEN 128

#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_HOME   0x84
#define KEY_END    0x85
#define KEY_DELETE 0x86

/* special key defines */
#define KEY_WIN  0x1001

struct yf_header {
    uint32_t magic;      /* 'CORN' */
    uint32_t version;
    uint32_t load_addr;  /* 0x100000 */
    uint32_t entry;      /* 0x100010 or wherever _start lands */
    uint32_t file_size;  /* bytes in the file */
    uint32_t bss_size;   /* loader clears this after the image */
    uint32_t checksum;
};

static volatile u16 *const VGA = (volatile u16 *)0xB8000;
static int cur_x = 0, cur_y = 0;
static u8 color = 0x07;
static int con_w = VGA_W, con_h = VGA_H;   /* real console size, set in kernel_main */
static void (*out_hook)(char);   /* when set, kputc output goes here instead of the screen */

/* Hardware port input */
static inline u8 inb(u16 port)
{
    u8 r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

/* Hardware port output */
static inline void outb(u16 port, u8 v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

/* Update the hardware cursor */
static void move_cursor(void)
{
    u16 pos = cur_y * VGA_W + cur_x;

    outb(0x3D4, 14);
    outb(0x3D5, (u8)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (u8)(pos & 0xFF));
}

/* Scroll the terminal */
static void scroll(void)
{
    int i;

    for (i = 0; i < VGA_W * (VGA_H - 1); i++)
        VGA[i] = VGA[i + VGA_W];

    for (i = VGA_W * (VGA_H - 1); i < VGA_W * VGA_H; i++)
        VGA[i] = (color << 8) | ' ';

    cur_y = VGA_H - 1;
}

int fbcon_init(void);
int fbcon_on(void);
void fbcon_putc(char ch, unsigned char attr);
void fbcon_clear(unsigned char attr);
void fbcon_set_cursor(u32 x, u32 y);
void fbcon_draw_at(char ch, u32 col, u32 row, u8 attr);
void wm_draw_desktop(void);
void wm_draw_about(void);

static void kputc(char c)
{
    int fb;

    if (out_hook) { out_hook(c); return; }

    fb = fbcon_on();

    if (fb)
        fbcon_putc(c, color);

    if (c == '\n') {
        cur_x = 0;
        cur_y++;
    } else if (c == '\b') {
        if (cur_x > 0) {
            cur_x--;
            if (!fb)
                VGA[cur_y * VGA_W + cur_x] = (color << 8) | ' ';
        }
    } else {
        if (!fb)
            VGA[cur_y * VGA_W + cur_x] = (color << 8) | (u8)c;
        cur_x++;
    }

    if (cur_x >= con_w) {
        cur_x = 0;
        cur_y++;
    }

    if (cur_y >= con_h) {
        if (fb)
            cur_y = con_h - 1;   /* fbcon scrolls itself */
        else
            scroll();
    }

    move_cursor();
}

/* Print a string */
static void kputs(const char *s)
{
    while (*s)
        kputc(*s++);
}

/* Print an unsigned integer */
static void kputu(u32 n)
{
    char tmp[11];
    int i = 0;

    if (n == 0) {
        kputc('0');
        return;
    }

    while (n > 0) {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i > 0)
        kputc(tmp[--i]);
}

/* Print an integer using at least two digits */
static void print_two_digits(unsigned int value)
{
    if (value < 10)
        kputc('0');

    kputu(value);
}

/* Terminal spinner: / | \ - */
static void spinner(void)
{
    static const char frames[] = "/|\\-";
    static u32 frame = 0;

    u16 pos = cur_y * VGA_W + cur_x;

    VGA[pos] = (color << 8) | frames[frame++ & 3];
    move_cursor();
}

/* String length */
static u32 klen(const char *s)
{
    u32 n = 0;

    while (s[n])
        n++;

    return n;
}

/* Clear the terminal */
static void kclear(void)
{
    int i;

    for (i = 0; i < VGA_W * VGA_H; i++)
        VGA[i] = (color << 8) | ' ';

    cur_x = 0;
    cur_y = 0;

    if (fbcon_on())
        fbcon_clear(color);

    move_cursor();
}

/* fi-fi layout, scancode set 1. ä ö å use CP437 codes (VGA text mode font). */
#define A_UML   0x84   /* ä */
#define O_UML   0x94   /* ö */
#define A_RING  0x86   /* å */
#define AU_UML  0x8E   /* Ä */
#define OU_UML  0x99   /* Ö */
#define AU_RING 0x8F   /* Å */

static const u8 map[128] = {
    [0x01]=27, [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
    [0x0C]='+',[0x0D]='\'',[0x0E]='\b',[0x0F]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',[0x15]='y',
    [0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',[0x1A]=A_RING,
    [0x1C]='\n',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',[0x23]='h',
    [0x24]='j',[0x25]='k',[0x26]='l',[0x27]=O_UML,[0x28]=A_UML,
    [0x2B]='\'',
    [0x2C]='z',[0x2D]='x',[0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',
    [0x32]='m',[0x33]=',',[0x34]='.',[0x35]='-',
    [0x37]='*',[0x39]=' ',[0x56]='<'
};

static const u8 map_shift[128] = {
    [0x01]=27, [0x02]='!',[0x03]='"',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='&',[0x08]='/',[0x09]='(',[0x0A]=')',[0x0B]='=',
    [0x0C]='?',[0x0D]='`',[0x0E]='\b',[0x0F]='\t',
    [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',[0x15]='Y',
    [0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',[0x1A]=AU_RING,
    [0x1B]='^',[0x1C]='\n',
    [0x1E]='A',[0x1F]='S',[0x20]='D',[0x21]='F',[0x22]='G',[0x23]='H',
    [0x24]='J',[0x25]='K',[0x26]='L',[0x27]=OU_UML,[0x28]=AU_UML,
    [0x2B]='*',
    [0x2C]='Z',[0x2D]='X',[0x2E]='C',[0x2F]='V',[0x30]='B',[0x31]='N',
    [0x32]='M',[0x33]=';',[0x34]=':',[0x35]='_',
    [0x37]='*',[0x39]=' ',[0x56]='>'
};

static const u8 map_altgr[128] = {
    [0x03]='@',[0x04]='$',
    [0x08]='{',[0x09]='[',[0x0A]=']',[0x0B]='}',
    [0x0C]='\\',[0x1B]='~',[0x56]='|'
};

static int is_letter(u8 c)
{
    return (c >= 'a' && c <= 'z') || c == A_UML || c == O_UML || c == A_RING;
}


/* Read a keyboard character */
/* Non-blocking: handles one scancode, returns a key or 0. */
static int key_poll(void)
{
    static int shift = 0, altgr = 0, caps = 0, ext = 0;
    u8 st = inb(0x64), sc, code, base, c;
    int release, was_ext, upper;

    if ((st & 0x21) != 0x01)            /* nothing, or a mouse byte */
        return 0;

    sc = inb(0x60);

    if (sc == 0xE0) { ext = 1; return 0; }

    was_ext = ext;
    ext = 0;
    release = sc & 0x80;
    code = sc & 0x7F;

    if (!was_ext && (code == 0x2A || code == 0x36)) { shift = !release; return 0; }
    if (was_ext && code == 0x38) { altgr = !release; return 0; }
	if (sc == 0x5B || sc == 0x5C) return KEY_WIN;
    if (!was_ext && code == 0x3A && !release) { caps = !caps; return 0; }
    if (release) return 0;

    if (was_ext) {
        switch (code) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4B: return KEY_LEFT;
            case 0x4D: return KEY_RIGHT;
            case 0x47: return KEY_HOME;
            case 0x4F: return KEY_END;
            case 0x53: return KEY_DELETE;
        }
        return 0;
    }

    if (altgr && map_altgr[code])
        return map_altgr[code];

    base = map[code];
    upper = shift ^ (caps && is_letter(base));
    c = upper ? map_shift[code] : base;
    return c;                           /* 0 if unmapped */
}

/* Blocking: waits for a key, discarding stray mouse bytes. */
static u8 getkey(void)
{
    for (;;) {
        u8 k, st = inb(0x64);

        if ((st & 0x21) == 0x21) {      /* mouse byte, nobody wants it */
            inb(0x60);
            continue;
        }
        k = key_poll();
        if (k)
            return k;
    }
}

/* Compare strings */
static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }

    return *a == *b;
}

/* Check string prefix */
static int startswith(const char *s, const char *p)
{
    while (*p) {
        if (*s++ != *p++)
            return 0;
    }

    return 1;
}

/* System information */
static void fetch(void)
{
    color = 0x0E;
    kputs("\n  ><(((('>   ");

    color = 0x0B;
    kputs("fish");

    color = 0x07;
    kputs("@");

    color = 0x0E;
    kputs("yellowfin\n");

    color = 0x07;
    kputs("              ---------------\n");

    color = 0x0E;
    kputs("              OS: ");

    color = 0x07;
    kputs("Yellowfin OS 0.3\n");

    color = 0x0E;
    kputs("              Arch: ");

    color = 0x07;
    kputs("i686\n");

    color = 0x0E;
    kputs("              Shell: ");

    color = 0x07;
    kputs("fsh\n\n");
}

static int key_available(void)
{
    return (inb(0x64) & 1) != 0;
}

static int kbd_ready(void)
{
    u8 st = inb(0x64);

    return (st & 1) && !(st & 0x20);
}

static u32 http_shown;

/* Prints at most ~1800 characters of a response, skipping CR and anything
   unprintable. */
static void http_sink(const u8 *d, u16 n)
{
    u16 i;

    for (i = 0; i < n && http_shown < 1800; i++) {
        u8 ch = d[i];

        if (ch == '\r')
            continue;
        if (ch == '\n' || (ch >= 32 && ch < 127)) {
            kputc((char)ch);
            http_shown++;
        }
    }
}

/* Command interpreter */
static void run(char *cmd)
{
    if (cmd[0] == 0)
        return;

	  kdebug("fsh", "exec: %s", cmd);      /* <-- add this */

    if (streq(cmd, "help")) {
    kputs("Commands: help, ver, gfxtest, desktop, clear, echo <text>, fetch, reboot\n");
    kputs("System:   time, date, timer, spin, free, dmesg\n");
    kputs("Files:    ls, write <name> <text>, cat <name>, rm <name>, df, format, savelog\n");
    kputs("Network:  netinfo, netstat, iptest, arp_test, ping <ip>\n");
    kputs("          nslookup <name>, httpget <host> [/path]\n");
    kputs("System:   install, crash <ud|div|gp>\n");
	
	    } else if (streq(cmd, "netinfo")) {
        struct pci_device dev;

        kputs("Network device scan\n");

        if (e1000_detect(&dev)) {
            kputs("E1000 detected\n");
            kputs("PCI address: ");
            kputu(dev.bus);
            kputc(':');
            kputu(dev.slot);
            kputc('.');
            kputu(dev.function);
            kputc('\n');

            kputs("Vendor ID: 0x8086\n");
            kputs("Device ID: 0x100E\n");
            kputs("PCI class: ");
            kputu(dev.class_code);
            kputc(':');
            kputu(dev.subclass);
            kputc('\n');
        } else {
            kputs("E1000 not found\n");
            kputs("Check QEMU's -nic option.\n");
        }

    } else if (streq(cmd, "ver")) {
        kputs("Yellowfin OS 0.3\n");
		kputs("The Network Update\n");
        kputs("Codename: Fish\n");
        kputs("Architecture: i686\n");
        kputs("Shell: fsh\n");
        kputs("Filesystem: FTFS\n");

    } else if (streq(cmd, "clear")) {
        kclear();
		
    } else if (streq(cmd, "desktop")) {
        if (!fbcon_on()) {
            kputs("desktop: VBE framebuffer unavailable\n");
            return;
        }
		
		desktop_run(key_poll);
        kclear();

    } else if (startswith(cmd, "echo ")) {
        kputs(cmd + 5);
        kputc('\n');

    } else if (streq(cmd, "fetch")) {
        fetch();

    } else if (streq(cmd, "reboot")) {
        while (inb(0x64) & 2) {
        }

        outb(0x64, 0xFE);

    } else if (streq(cmd, "timer")) {
        kputs("Timer ticks: ");
        kputu(timer_ticks());
        kputc('\n');

        kputs("Uptime: ");
        kputu(timer_seconds());
        kputs(" seconds\n");

    } else if (streq(cmd, "time")) {
        struct rtc_datetime dt;

        if (!rtc_get_datetime(&dt)) {
            kputs("RTC read failed\n");
            return;
        }

        print_two_digits(dt.hour);
        kputc(':');
        print_two_digits(dt.minute);
        kputc(':');
        print_two_digits(dt.second);
        kputc('\n');

    } else if (streq(cmd, "date")) {
        struct rtc_datetime dt;

        if (!rtc_get_datetime(&dt)) {
            kputs("RTC read failed\n");
            return;
        }

        print_two_digits(dt.day);
        kputc('/');
        print_two_digits(dt.month);
        kputc('/');
        kputu(dt.year);
        kputc('\n');

    } else if (streq(cmd, "ls")) {
        char name[20];
        u32 size;
        int i;

        for (i = 0; ftfs_entry(i, name, &size) == 0; i++) {
            kputs(name);
            kputs("  ");
            kputu(size);
            kputs(" bytes\n");
        }

        if (i == 0)
            kputs("(no files)\n");

    } else if (startswith(cmd, "write ")) {
        char *name = cmd + 6;
        char *text = name;
        int r;

        while (*text && *text != ' ')
            text++;

        if (*text == ' ') {
            *text = 0;
            text++;
        }

        r = ftfs_write(name, (const u8 *)text, klen(text));

        if (r == 0)
            kputs("saved\n");
        else if (r == -1)
            kputs("write: bad name (1-19 characters)\n");
        else if (r == -2)
            kputs("write: file table full\n");
        else
            kputs("write: no space left\n");

    } else if (startswith(cmd, "cat ")) {
        char data[256];
        int n = ftfs_read(cmd + 4, (u8 *)data, 255);

        if (n < 0) {
            kputs("cat: no such file\n");
        } else {
            data[n] = 0;
            kputs(data);
            kputc('\n');
        }

    } else if (startswith(cmd, "rm ")) {
        if (ftfs_delete(cmd + 3) == 0)
            kputs("deleted\n");
        else
            kputs("rm: no such file\n");

    } else if (streq(cmd, "df")) {
        kputu(ftfs_free_blocks());
        kputs(" of 65403 blocks free (512 bytes each)\n");

    } else if (streq(cmd, "format")) {
        ftfs_format();
        kputs("FTFS formatted\n");

      } else if (streq(cmd, "spin")) {
        int j;
        char c;

        kputs("loading ");

        for (;;) {
            spinner();

            for (j = 0; j < 30000000; j++)
                __asm__ volatile ("nop");

            if (key_available()) {
                c = inb(0x60);
				


                if (!(c & 0x80))
                    break;
            }
        }
		
		

        kputc('\b');
        kputc(' ');
        kputc('\n');
    } else if (streq(cmd, "dmesg")) {
        uint32_t len;
        u32 i, lines = 0;
        const char *lb = klog_buffer(&len);

        for (i = 0; i < len; i++) {
            kputc(lb[i]);
            if (lb[i] == '\n' && ++lines == con_h - 2) {
                u8 k;
                int j;
                kputs("-- more (q quits) --");
                k = getkey();
                for (j = 0; j < 20; j++) kputc('\b');
                if (k == 'q') break;
                lines = 0;
            }
        }

    } else if (streq(cmd, "savelog")) {
        uint32_t len;
        const char *lb = klog_buffer(&len);

        if (ftfs_write("boot.log", (const u8 *)lb, len) == 0) {
            kputs("saved boot.log (");
            kputu(len);
            kputs(" bytes)\n");
        } else {
            kputs("savelog: write failed\n");
        }

    } else if (startswith(cmd, "crash ")) {
        if (streq(cmd + 6, "ud"))
            __asm__ volatile ("ud2");
        else if (streq(cmd + 6, "div"))
            __asm__ volatile ("xorl %%ecx, %%ecx\n\tdivl %%ecx" : : : "eax", "ecx", "edx");
        else if (streq(cmd + 6, "gp"))
            __asm__ volatile ("movw $0xFFF8, %%ax\n\tmovw %%ax, %%ds" : : : "eax");
        else
            kputs("crash: ud, div or gp\n");

    } else if (streq(cmd, "free")) {
        kputs("Total usable RAM: ");
        kputu(mem_usable_kb() / 1024);
        kputs(" MB (");
        kputu(mem_usable_kb());
        kputs(" KB)\n");
		
	    } else if (streq(cmd, "gfxtest")) {
        if (!gfx_init()) {
            kputs("gfxtest: no graphics mode\n");
            return;
        }

        gfx_vgradient(0, 0, (int)gfx_w(), (int)gfx_h(), 0x1A4A7A, 0x0A1A30);

        gfx_blend_rect(236, 186, 520, 320, 0x000000, 90);     /* shadow */
        gfx_blend_rect(230, 180, 520, 320, 0xA0C8F0, 140);    /* glass body */
        gfx_vgradient(231, 181, 518, 30, 0x6FA8E0, 0x3A78B8); /* title bar */
        gfx_frame(230, 180, 520, 320, 0xFFFFFF);
        gfx_text(244, 188, "Glass test", 0xFFFFFF);
        gfx_rect(696, 184, 44, 20, 0xC04040);
        gfx_text(712, 186, "X", 0xFFFFFF);

        gfx_flip();
        getkey();
        kclear();
		
	} else if (streq(cmd, "install")) {
        installer_run(getkey);
        kclear();
		

} else if (streq(cmd, "nettest")) {
    u8 frame[60];
    u8 rx_frame[2048];
    int i;
    int sent;
    int received;
    int found = 0;
    u32 start;

    static const u8 mac[6] = {
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56
    };

    if (!e1000_is_ready()) {
        kputs("e1000 is not initialized.\n");
        return;
    }

    /* Clear Ethernet frame */
    for (i = 0; i < 60; i++)
        frame[i] = 0;

    /* Ethernet destination: broadcast */
    for (i = 0; i < 6; i++)
        frame[i] = 0xFF;

    /* Ethernet source MAC */
    for (i = 0; i < 6; i++)
        frame[6 + i] = mac[i];

    /* EtherType: ARP (0x0806) */
    frame[12] = 0x08;
    frame[13] = 0x06;

    /* ARP header */
    frame[14] = 0x00; /* Ethernet hardware */
    frame[15] = 0x01;
    frame[16] = 0x08; /* IPv4 protocol */
    frame[17] = 0x00;
    frame[18] = 6;    /* MAC length */
    frame[19] = 4;    /* IPv4 length */
    frame[20] = 0x00; /* ARP request */
    frame[21] = 0x01;

    /* Sender MAC */
    for (i = 0; i < 6; i++)
        frame[22 + i] = mac[i];

    /* Sender IPv4: 10.0.2.15 */
    frame[28] = 10;
    frame[29] = 0;
    frame[30] = 2;
    frame[31] = 15;

    /* Target MAC: unknown, leave zero */

    /* Target IPv4: 10.0.2.2 */
    frame[38] = 10;
    frame[39] = 0;
    frame[40] = 2;
    frame[41] = 2;

    kputs("Sending ARP request for 10.0.2.2...\n");

    sent = e1000_send(frame, 60);

    if (!sent) {
        kputs("ARP TX: FAILED\n");
        return;
    }

    kputs("ARP TX: SUCCESS\n");
    kputs("Waiting for ARP reply...\n");

    start = timer_ticks();

    while ((u32)(timer_ticks() - start) < 200) {
        received = e1000_receive(rx_frame, sizeof(rx_frame));

        if (received >= 42) {
            /* Ethernet EtherType must be ARP */
            if (rx_frame[12] == 0x08 &&
                rx_frame[13] == 0x06) {

                /* ARP opcode must be reply (2) */
                if (rx_frame[20] == 0x00 &&
                    rx_frame[21] == 0x02) {

                    /* Sender IP must be 10.0.2.2 */
                    if (rx_frame[28] == 10 &&
                        rx_frame[29] == 0 &&
                        rx_frame[30] == 2 &&
                        rx_frame[31] == 2) {

                        found = 1;
                        break;
                    }
                }
            }
        }
    }

    if (found) {
        kputs("ARP RX: GATEWAY REPLIED!\n");

        kdebug("arp", "gateway MAC %02x:%02x:%02x:%02x:%02x:%02x",
            rx_frame[22], rx_frame[23],
            rx_frame[24], rx_frame[25],
            rx_frame[26], rx_frame[27]);
    } else {
        kputs("ARP RX: NO GATEWAY REPLY\n");
        e1000_rx_debug();
    }

    } else if (streq(cmd, "arp_test")) {
        static const u8 gateway_ip[4] = {
            10, 0, 2, 2
        };

        u8 gateway_mac[6];

        kputs("Resolving gateway 10.0.2.2...\n");

        if (arp_resolve(gateway_ip, gateway_mac)) {
            kputs("ARP: gateway resolved!\n");

            kdebug("arp",
                "gateway MAC %02x:%02x:%02x:%02x:%02x:%02x",
                gateway_mac[0], gateway_mac[1],
                gateway_mac[2], gateway_mac[3],
                gateway_mac[4], gateway_mac[5]);
        } else {
            kputs("ARP: resolution failed.\n");
        }

} else if (streq(cmd, "netstat")) {
    ethernet_poll();    /* drain anything pending so the counters are current */

    kputs("n\Ethernet statistics");
    kputs("  rx frames:   "); kputu(eth_stats.rx_frames);   kputc('\n');
    kputs("  rx arp:      "); kputu(eth_stats.rx_arp);      kputc('\n');
    kputs("  rx ipv4:     "); kputu(eth_stats.rx_ipv4);     kputc('\n');
    kputs("  rx ignored:  "); kputu(eth_stats.rx_ignored);  kputc('\n');
    kputs("  rx filtered: "); kputu(eth_stats.rx_filtered); kputc('\n');
    kputs("  rx short:    "); kputu(eth_stats.rx_short);    kputc('\n');
    kputs("  tx frames:   "); kputu(eth_stats.tx_frames);   kputc('\n');
    kputs("  tx errors:   "); kputu(eth_stats.tx_errors);   kputc('\n');
	    kputs("n\IPv4 statistics");
    kputs("  rx total:    "); kputu(ipv4_stats.rx_total);     kputc('\n');
    kputs("  rx ok:       "); kputu(ipv4_stats.rx_ok);        kputc('\n');
    kputs("  bad length:  "); kputu(ipv4_stats.bad_length);   kputc('\n');
    kputs("  bad header:  "); kputu(ipv4_stats.bad_header);   kputc('\n');
    kputs("  bad cksum:   "); kputu(ipv4_stats.bad_checksum); kputc('\n');
    kputs("  not for us:  "); kputu(ipv4_stats.not_for_us);   kputc('\n');
    kputs("  fragments:   "); kputu(ipv4_stats.fragments);    kputc('\n');
    kputs("  icmp:        "); kputu(ipv4_stats.rx_icmp);      kputc('\n');
    kputs("  other proto: "); kputu(ipv4_stats.rx_other);     kputc('\n');
	    kputs("\nICMP statistics");
    kputs("  echo req rx: "); kputu(icmp_stats.rx_echo_req);   kputc('\n');
    kputs("  echo rep tx: "); kputu(icmp_stats.tx_echo_reply); kputc('\n');
    kputs("  echo rep rx: "); kputu(icmp_stats.rx_echo_reply); kputc('\n');
    kputs("  unmatched:   "); kputu(icmp_stats.rx_unmatched);  kputc('\n');
    kputs("  bad cksum:   "); kputu(icmp_stats.bad_checksum);  kputc('\n');
    kputs("  bad length:  "); kputu(icmp_stats.bad_length);    kputc('\n');
	    kputs("\nUDP statistics\n");
    kputs("  rx total:    "); kputu(udp_stats.rx_total);     kputc('\n');
    kputs("  delivered:   "); kputu(udp_stats.rx_delivered); kputc('\n');
    kputs("  no listener: "); kputu(udp_stats.no_listener);  kputc('\n');
    kputs("  bad length:  "); kputu(udp_stats.bad_length);   kputc('\n');
    kputs("  bad cksum:   "); kputu(udp_stats.bad_checksum); kputc('\n');
    kputs("  tx total:    "); kputu(udp_stats.tx_total);     kputc('\n');
	    kputs("\nTCP statistics\n");
    kputs("  rx segments: "); kputu(tcp_stats.rx_segments);  kputc('\n');
    kputs("  tx segments: "); kputu(tcp_stats.tx_segments);  kputc('\n');
    kputs("  retransmits: "); kputu(tcp_stats.retransmits);  kputc('\n');
    kputs("  resets rx:   "); kputu(tcp_stats.resets_rx);    kputc('\n');
    kputs("  resets sent: "); kputu(tcp_stats.resets_sent);  kputc('\n');
    kputs("  bad cksum:   "); kputu(tcp_stats.bad_checksum); kputc('\n');
    kputs("  bad length:  "); kputu(tcp_stats.bad_length);   kputc('\n');
	
} else if (streq(cmd, "iptest")) {
    int r = ipv4_selftest();

    if (r == 0) {
        kputs("ipv4 selftest: all cases passed\n");
    } else {
        kputs("ipv4 selftest: FAILED at case ");
        kputu((u32)r);
        kputc('\n');
    }

} else if (startswith(cmd, "ping ")) {
    u8 dst[4];
    int n, k;
    u32 got = 0;

    if (!ipv4_parse(cmd + 5, dst)) {
        kputs("Usage: ping a.b.c.d\n");
    } else if (!e1000_is_ready()) {
        kputs("e1000 is not initialized.\n");
    } else {
        kputs("Pinging ");
        for (k = 0; k < 4; k++) {
            kputu(dst[k]);
            if (k < 3) kputc('.');
        }
        kputs(" with 32 bytes of data:\n");

        for (n = 0; n < 4; n++) {
            u32 rtt = 0;
            u8  ttl = 0;
            u32 t0 = net_now_ms();
            int r = icmp_ping(dst, (u16)(n + 1), 2000, &rtt, &ttl);

            if (r == ICMP_OK) {
                got++;
                kputs("Reply from ");
                for (k = 0; k < 4; k++) {
                    kputu(dst[k]);
                    if (k < 3) kputc('.');
                }
                kputs(": time=");
                if (rtt < 10) kputs("<10"); else kputu(rtt);
                kputs("ms ttl=");
                kputu(ttl);
                kputc('\n');
            } else if (r == ICMP_ERR_TIMEOUT) {
                kputs("Request timed out.\n");
            } else if (r == ICMP_ERR_NOROUTE) {
                kputs("Destination unreachable (ARP failed).\n");
            } else {
                kputs("Send failed.\n");
            }

            /* one ping per second */
            while ((u32)(net_now_ms() - t0) < 1000)
                ethernet_poll();
        }

        kputs("Sent = 4, received = ");
        kputu(got);
        kputc('\n');
    }

} else if (startswith(cmd, "nslookup ")) {
    u8 ip[4];
    int r, k;
    const char *name = cmd + 9;

    while (*name == ' ')
        name++;

    if (!*name) {
        kputs("Usage: nslookup <name>\n");
    } else if (!e1000_is_ready()) {
        kputs("e1000 is not initialized.\n");
    } else {
        const u8 *srv = dns_server();

        kputs("Server:  ");
        for (k = 0; k < 4; k++) {
            kputu(srv[k]);
            if (k < 3) kputc('.');
        }
        kputs("\nName:    ");
        kputs(name);
        kputc('\n');

        r = dns_resolve(name, ip, 2000);

        if (r == DNS_OK) {
            kputs("Address: ");
            for (k = 0; k < 4; k++) {
                kputu(ip[k]);
                if (k < 3) kputc('.');
            }
            kputc('\n');
        } else if (r == DNS_ERR_NXDOMAIN) {
            kputs("Name does not exist.\n");
        } else if (r == DNS_ERR_NOADDR) {
            kputs("No IPv4 address for that name.\n");
        } else if (r == DNS_ERR_NAME) {
            kputs("Invalid name.\n");
        } else if (r == DNS_ERR_NET) {
            kputs("Network error (could not reach the DNS server).\n");
        } else if (r == DNS_ERR_SERVER) {
            kputs("DNS server error.\n");
        } else {
            kputs("DNS request timed out.\n");
        }
    }

} else if (startswith(cmd, "httpget ")) {
    char host[64];
    char path[96];
    const char *s = cmd + 8;    int hn = 0, pn = 0;
    u32 total = 0;
    int r;

    while (*s == ' ')
        s++;
    while (*s && *s != ' ' && hn < 63)
        host[hn++] = *s++;
    host[hn] = 0;

    while (*s == ' ')
        s++;
    if (*s == 0) {
        path[0] = '/';
        path[1] = 0;
    } else {
        while (*s && pn < 95)
            path[pn++] = *s++;
        path[pn] = 0;
    }

    if (hn == 0 || path[0] != '/') {
        kputs("Usage: httpget <host> [/path]\n");
    } else if (!e1000_is_ready()) {
        kputs("e1000 is not initialized.\n");
    } else {
        kputs("GET http://");
        kputs(host);
        kputs(path);
        kputs("\n\n");

        http_shown = 0;
        r = http_get(host, 80, path, http_sink, &total);

        kputs("\n\n--- ");
        if (r == HTTP_OK) {
            kputu(total);
            kputs(" bytes received");
        } else if (r == HTTP_ERR_DNS) {
            kputs("DNS lookup failed");
        } else if (r == HTTP_ERR_CONNECT) {
            kputs("could not connect (timed out)");
        } else if (r == HTTP_ERR_REFUSED) {
            kputs("connection refused");
        } else if (r == HTTP_ERR_REQUEST) {
            kputs("host or path too long");
        } else if (r == HTTP_ERR_SEND) {
            kputs("send failed");
        } else if (r == HTTP_ERR_RESET) {
            kputs("connection reset by peer");
        } else {
            kputs("receive timed out");
        }
        kputc('\n');
    }

} else if (streq(cmd, "mousetest")) {
    u32 loops = 0, any = 0, aux = 0, last = timer_ticks();
    u8 laststat = 0xFF;

    gfx_init();
    mouse_init();
    kputs("Move the mouse. Any key quits.\n");

    while (!kbd_ready()) {
        u8 st = inb(0x64);
        loops++;

        if (st != laststat) {
            kdebug("mt", "status %02x at loop %u", st, loops);
            laststat = st;
        }

        if (st & 1) {
            u8 b = inb(0x60);
            any++;
            if (st & 0x20) {
                aux++;
                kdebug("mt", "aux byte %02x", b);
            } else {
                kdebug("mt", "other byte %02x", b);
            }
        }

        if (timer_ticks() - last >= 100) {          /* once per second */
            last = timer_ticks();
            kdebug("mt", "loops=%u any=%u aux=%u", loops, any, aux);
        }
		
		
		
    }
    getkey();
    kputc('\n');
    kinfo("mousetest", "output-full=%u from-mouse=%u", any, aux);
    } else {
        kputs("Unknown command. Type 'help'.\n");
    }
}

void fsh_exec(const char *line, void (*sink)(char))
{
    char buf[MAXLEN];
    int i;

    for (i = 0; i < MAXLEN - 1 && line[i]; i++)
        buf[i] = line[i];
    buf[i] = 0;

    out_hook = sink;
    run(buf);
    out_hook = 0;
    color = 0x07;
}

/* Shell prompt */
static void prompt(void)
{
    color = 0x0B;
    kputs("fish");

    color = 0x07;
    kputs("@");

    color = 0x0E;
    kputs("yellowfin");

    color = 0x07;
    kputs(" ~ $ ");

    color = 0x0F;
}

static void log_sink(const char *s, int level)
{
    static const u8 colors[] = { 0x08, 0x07, 0x0E, 0x0C, 0x0A };
    u8 old = color;
    color = colors[level];
    kputs(s);
    color = old;
}

void kernel_panic(const char *reason)
{
    uint32_t len, i;
    const char *tail;

    __asm__ volatile ("cli");
    kerr("kernel", "PANIC: %s", reason);

    color = 0x1F;                       /* white on blue */
    kclear();

    kputs("\n  /\\_/\\\n");
    kputs(" ( x.x )   ><(((('>   C A T Q U A R I U M\n");
    kputs("  > ^ <\n\n");
    kputs(" Kernel panic: the cat knocked over the aquarium.\n");
    kputs(" Reason: ");
    kputs(reason);
    kputs("\n\n Last log lines:\n");

    tail = klog_tail(10, &len);
    for (i = 0; i < len; i++)
        kputc(tail[i]);

    kputs("\n System halted. Restart the machine.\n");

    for (;;)
        __asm__ volatile ("hlt");
}



/* Shell command history and line editor */
#define HISTORY_SIZE 8

static char history[HISTORY_SIZE][MAXLEN];
static int history_count = 0;
static int history_next = 0;

static int input_start_x;
static int input_start_y;
static int input_drawn_len;

static void history_add(const char *s)
{
    int i;
    int last;

    if (!s || !s[0])
        return;

    /* Avoid consecutive duplicate commands. */
    if (history_count > 0) {
        last = (history_next + HISTORY_SIZE - 1)
               % HISTORY_SIZE;

        if (streq(history[last], s))
            return;
    }

    for (i = 0; i < MAXLEN - 1 && s[i]; i++)
        history[history_next][i] = s[i];

    history[history_next][i] = '\0';

    history_next = (history_next + 1) % HISTORY_SIZE;

    if (history_count < HISTORY_SIZE)
        history_count++;
}




static void redraw_line(const char *buf, int len, int pos)
{
    int i;
    int count = input_drawn_len > len ? input_drawn_len : len;

    for (i = 0; i < count; i++) {
        char c = (i < len) ? buf[i] : ' ';

        if (fbcon_on()) {
            fbcon_draw_at(c, input_start_x + i, input_start_y, color);
        } else {
            int p = input_start_y * VGA_W + input_start_x + i;
            if (p >= 0 && p < VGA_W * VGA_H)
                VGA[p] = ((u16)color << 8) | (u8)c;
        }
    }

    input_drawn_len = len;
    cur_x = input_start_x + pos;
    cur_y = input_start_y;

    if (fbcon_on())
        fbcon_set_cursor(cur_x, cur_y);

    move_cursor();
}


static int read_line(char *buf, int maxlen)
{
    int len = 0;
    int pos = 0;
    int hist_offset = 0;
    int max_chars;
    int i;

    if (maxlen < 1)
        return 0;

    buf[0] = '\0';

    input_start_x = cur_x;
    input_start_y = cur_y;
    input_drawn_len = 0;

    /* Keep input on the current screen row. */
    max_chars = VGA_W - input_start_x - 1;

    if (max_chars > maxlen - 1)
        max_chars = maxlen - 1;

    if (max_chars < 0)
        max_chars = 0;
	
	redraw_line(buf, len, pos);

    for (;;) {
        u8 c = getkey();

        if (c == '\n') {
            pos = len;
            redraw_line(buf, len, pos);
            kputc('\n');
            history_add(buf);
            return len;
        }

        /* Backspace */
        if (c == '\b') {
            if (pos > 0) {
                for (i = pos - 1; i < len - 1; i++)
                    buf[i] = buf[i + 1];

                len--;
                pos--;
                buf[len] = '\0';
                redraw_line(buf, len, pos);
            }
            continue;
        }

        /* Delete */
        if (c == KEY_DELETE) {
            if (pos < len) {
                for (i = pos; i < len - 1; i++)
                    buf[i] = buf[i + 1];

                len--;
                buf[len] = '\0';
                redraw_line(buf, len, pos);
            }
            continue;
        }

        /* Left and right arrows */
        if (c == KEY_LEFT) {
            if (pos > 0) {
                pos--;
                redraw_line(buf, len, pos);
            }
            continue;
        }

        if (c == KEY_RIGHT) {
            if (pos < len) {
                pos++;
                redraw_line(buf, len, pos);
            }
            continue;
        }

        /* Home and End */
        if (c == KEY_HOME) {
            pos = 0;
            redraw_line(buf, len, pos);
            continue;
        }

        if (c == KEY_END) {
            pos = len;
            redraw_line(buf, len, pos);
            continue;
        }

        /* Command history */
        if (c == KEY_UP || c == KEY_DOWN) {
            if (c == KEY_UP) {
                if (hist_offset < history_count)
                    hist_offset++;
            } else {
                if (hist_offset > 0)
                    hist_offset--;
            }

            if (hist_offset == 0) {
                len = 0;
                pos = 0;
                buf[0] = '\0';
            } else {
                int index;

                index = (history_next - hist_offset
                         + HISTORY_SIZE) % HISTORY_SIZE;

                len = 0;

                while (len < max_chars &&
                       history[index][len] != '\0') {
                    buf[len] = history[index][len];
                    len++;
                }

                buf[len] = '\0';
                pos = len;
            }

            redraw_line(buf, len, pos);
            continue;
        }

        /* Insert printable ASCII characters. */
        if (c >= ' ' && c < 0x80 && len < max_chars) {
            for (i = len; i > pos; i--)
                buf[i] = buf[i - 1];

            buf[pos] = (char)c;
            len++;
            pos++;
            buf[len] = '\0';

            redraw_line(buf, len, pos);
        }
    }
}


/* Kernel entry point */
void kernel_main(u32 magic, u32 mbi)
{
	payload_snapshot();
		
    char buf[MAXLEN];
		if (fbcon_init()) {
		con_w = *(volatile u16 *)0x9012 / 8;    /* width from VBE mode info */
        con_h = *(volatile u16 *)0x9014 / 16;   /* height from VBE mode info */
		const char *t = "Yellowfin graphics works!\n";
		for (int i = 0; t[i]; i++)
			fbcon_putc(t[i], 0x0E);
	}

    timer_init(100);
    kclear();

    klog_init();
    klog_set_sink(log_sink);
	

    kinfo("kernel", "Yellowfin OS 0.3 (Corn Kernel) i686");
    mem_init(magic, mbi);
    kinfo("timer", "PIT running at 100 Hz");
    klog_probe_cpu();

    {
        struct rtc_datetime dt;

        if (rtc_get_datetime(&dt))
            kinfo("rtc", "%u-%02u-%02u %02u:%02u:%02u",
                  dt.year, dt.month, dt.day,
                  dt.hour, dt.minute, dt.second);
        else
            kwarn("rtc", "read failed");
    }

    klog_probe_pci();

    {

{
    struct pci_device dev;

    if (e1000_detect(&dev)) {
        pci_enable(dev.bus, dev.slot, dev.function, 0x02);

        if (!e1000_init(&dev)) {
            kwarn("e1000", "initialization failed", 0);
        }
    } else {
        kwarn("e1000", "not found (check QEMU -nic)");
    }
}

    if (e1000_is_ready()) {
        static const u8 ip[4] = { 10, 0, 2, 15 };

        arp_init(ip);
        kok("arp", "initialized with IP 10.0.2.15");
    }

    {
        int m = ftfs_init();

        if (m == 0)
            kerr("ftfs", "no data disk (start QEMU with a data.img drive)");
        else if (m == 2)
            kwarn("ftfs", "blank data disk, formatted new FTFS");
        else
            kok("ftfs", "mounted, %u of 65403 blocks free",
                ftfs_free_blocks());
    }

    kok("kernel", "boot complete, starting fsh");
    kputs("\nType 'help' for commands.\n\n");

    for (;;) {
        prompt();
        read_line(buf, MAXLEN);

        color = 0x07;
        run(buf);
    }

	}
}
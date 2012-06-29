/* winegcc -m32 -g -Wall -Wextra loader.c -o loader -L. -lcxbx */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include <wine/library.h>

uint32_t *_CxbxKrnl_KernelThunkTable(void);
void CxbxKrnlInit(void *, void *, void *, void *, int, char *, void *, int, void *);

#define PACK                __attribute__((__packed__))

#define XBE_BASE_ADDR       0x10000
#define XBE_MAGIC           "XBEH"
#define XBE_XOR_EP_DEBUG    0x94859d4b      // Entry Point (Debug)
#define XBE_XOR_EP_RETAIL   0xa8fc57ab      // Entry Point (Retail)
#define XBE_XOR_KT_DEBUG    0xefb1f152      // Kernel Thunk (Debug)
#define XBE_XOR_KT_RETAIL   0x5b6d40b6      // Kernel Thunk (Retail)

typedef struct {
    uint32_t dwMagic;                       // 0x0000 - magic number [should be "XBEH"]
    uint8_t  pbDigitalSignature[256];       // 0x0004 - digital signature
    uint32_t dwBaseAddr;                    // 0x0104 - base address
    uint32_t dwSizeofHeaders;               // 0x0108 - size of headers
    uint32_t dwSizeofImage;                 // 0x010c - size of image
    uint32_t dwSizeofImageHeader;           // 0x0110 - size of image header
    uint32_t dwTimeDate;                    // 0x0114 - timedate stamp
    uint32_t dwCertificateAddr;             // 0x0118 - certificate address
    uint32_t dwSections;                    // 0x011c - number of sections
    uint32_t dwSectionHeadersAddr;          // 0x0120 - section headers address

    struct {                                // 0x0124 - initialization flags
        uint32_t bMountUtilityDrive   : 1;  // 0x0124 - mount utility drive flag
        uint32_t bFormatUtilityDrive  : 1;  // 0x0124 - format utility drive flag
        uint32_t bLimit64MB           : 1;  // 0x0124 - limit development kit run time memory to 64mb flag
        uint32_t bDontSetupHarddisk   : 1;  // 0x0124 - don't setup hard disk flag
        uint32_t Unused               : 4;  // 0x0124 - unused (or unknown)
        uint32_t Unused_b1            : 8;  // 0x0125 - unused (or unknown)
        uint32_t Unused_b2            : 8;  // 0x0126 - unused (or unknown)
        uint32_t Unused_b3            : 8;  // 0x0127 - unused (or unknown)
    } PACK dwInitFlags;

    uint32_t dwEntryAddr;                   // 0x0128 - entry point address
    uint32_t dwTLSAddr;                     // 0x012c - thread local storage directory address
    uint32_t dwPeStackCommit;               // 0x0130 - size of stack commit
    uint32_t dwPeHeapReserve;               // 0x0134 - size of heap reserve
    uint32_t dwPeHeapCommit;                // 0x0138 - size of heap commit
    uint32_t dwPeBaseAddr;                  // 0x013c - original base address
    uint32_t dwPeSizeofImage;               // 0x0140 - size of original image
    uint32_t dwPeChecksum;                  // 0x0144 - original checksum
    uint32_t dwPeTimeDate;                  // 0x0148 - original timedate stamp
    uint32_t dwDebugPathnameAddr;           // 0x014c - debug pathname address
    uint32_t dwDebugFilenameAddr;           // 0x0150 - debug filename address
    uint32_t dwDebugUnicodeFilenameAddr;    // 0x0154 - debug unicode filename address
    uint32_t dwKernelImageThunkAddr;        // 0x0158 - kernel image thunk address
    uint32_t dwNonKernelImportDirAddr;      // 0x015c - non kernel import directory address
    uint32_t dwLibraryVersions;             // 0x0160 - number of library versions
    uint32_t dwLibraryVersionsAddr;         // 0x0164 - library versions address
    uint32_t dwKernelLibraryVersionAddr;    // 0x0168 - kernel library version address
    uint32_t dwXAPILibraryVersionAddr;      // 0x016c - xapi library version address
    uint32_t dwLogoBitmapAddr;              // 0x0170 - logo bitmap address
    uint32_t dwSizeofLogoBitmap;            // 0x0174 - logo bitmap size
} PACK XbeHeader;

typedef struct {
    struct {                                // 0x0000 - section flags
        uint32_t bWritable        : 1;      // 0x0000 - writable flag
        uint32_t bPreload         : 1;      // 0x0000 - preload flag
        uint32_t bExecutable      : 1;      // 0x0000 - executable flag
        uint32_t bInsertedFile    : 1;      // 0x0000 - inserted file flag
        uint32_t bHeadPageRO      : 1;      // 0x0000 - head page read only flag
        uint32_t bTailPageRO      : 1;      // 0x0000 - tail page read only flag
        uint32_t Unused_a1        : 1;      // 0x0000 - unused (or unknown)
        uint32_t Unused_a2        : 1;      // 0x0000 - unused (or unknown)
        uint32_t Unused_b1        : 8;      // 0x0001 - unused (or unknown)
        uint32_t Unused_b2        : 8;      // 0x0002 - unused (or unknown)
        uint32_t Unused_b3        : 8;      // 0x0003 - unused (or unknown)
    } PACK dwFlags;

    uint32_t dwVirtualAddr;                 // 0x0004 - virtual address
    uint32_t dwVirtualSize;                 // 0x0008 - virtual size
    uint32_t dwRawAddr;                     // 0x000c - file offset to raw data
    uint32_t dwSizeofRaw;                   // 0x0010 - size of raw data
    uint32_t dwSectionNameAddr;             // 0x0014 - section name addr
    uint32_t dwSectionRefCount;             // 0x0018 - section reference count
    uint32_t dwHeadSharedRefCountAddr;      // 0x001c - head shared page reference count address
    uint32_t dwTailSharedRefCountAddr;      // 0x0020 - tail shared page reference count address
    uint8_t  bzSectionDigest[20];           // 0x0024 - section digest
} PACK XbeSectionHeader;

typedef struct {
    uint32_t dwDataStartAddr;               // 0x0000 - raw start address
    uint32_t dwDataEndAddr;                 // 0x0004 - raw end address
    uint32_t dwTLSIndexAddr;                // 0x0008 - tls index address
    uint32_t dwTLSCallbackAddr;             // 0x000c - tls callback address
    uint32_t dwSizeofZeroFill;              // 0x0010 - size of zero fill
    uint32_t dwCharacteristics;             // 0x0014 - characteristics
} PACK XbeTLS;

int
main(int argc, char **argv) {
    XbeHeader *xbeh = (void *)XBE_BASE_ADDR;
    void *hwnd;
    char *env;
    unsigned long start;
    size_t sz;
    int fd;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <xbe unix filepath> <window handle> [debug mode] [debug filepath]\n", argv[0]);
        return 1;
    }
    if (!(env = getenv("WINEADDRSPACESTART"))) {
        fprintf(stderr, "error: WINEADDRSPACESTART is not set in environment\n");
        return 1;
    }
    if (sscanf(env, "%lx", &start) != 1 || !start) {
        fprintf(stderr, "error: could not parse WINEADDRSPACESTART environment variable\n");
        return 1;
    }
    if (start < 0x330000) {
        fprintf(stderr, "error: WINEADDRSPACESTART should be set on or beyond 0x330000 (default wine thread/signal stack address)\n");
        return 1;
    }
    if (sscanf(argv[2], "%p", &hwnd) != 1 || !hwnd) {
        fprintf(stderr, "error: could not parse window handle argument\n");
        return 1;
    }
    if ((fd = open(argv[1], O_RDONLY)) < 0) {
        fprintf(stderr, "error: open: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    if (start - XBE_BASE_ADDR < (sz = lseek(fd, 0, SEEK_END))) {
        fprintf(stderr, "error: WINEADDRSPACESTART - XBE_BASE_ADDR < xbe size (0x%x); consider increasing WINEADDRSPACESTART\n", sz);
        close(fd);
        return 1;
    }

    /* 0x010000-0x110000 was dos area by reserve_dos_area() */
    /* 0x232000-0x330000 was wine thread/signal stack by virtual_alloc_thread_stack() */
    wine_mmap_remove_reserved_area(NULL, start, 1);
    if (mmap((void *)XBE_BASE_ADDR,
        sz,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE,
        fd, 0) != (void *)XBE_BASE_ADDR) {
        fprintf(stderr, "error: could not mmap xbe at 0x%x\n", XBE_BASE_ADDR);
        close(fd);
        return 1;
    }
    mmap((void *)XBE_BASE_ADDR + sz,
        start - XBE_BASE_ADDR - sz,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE,
        -1, 0);
    close(fd);
    wine_mmap_add_reserved_area(NULL, start);

    {
        char buf[32];
        snprintf(buf, sizeof(buf), "cat /proc/%hu/maps", getpid());
        system(buf);
    }

    if (xbeh->dwMagic != *(uint32_t *)XBE_MAGIC) {
        fprintf(stderr, "error: invalid xbe magic: 0x%.08x\n", xbeh->dwMagic);
        return 1;
    }
    xbeh->dwEntryAddr ^=
        ((xbeh->dwEntryAddr ^ XBE_XOR_EP_RETAIL) > 0x1000000)
            ? XBE_XOR_EP_DEBUG
            : XBE_XOR_EP_RETAIL;
    xbeh->dwKernelImageThunkAddr ^=
        ((xbeh->dwKernelImageThunkAddr ^ XBE_XOR_KT_RETAIL) > 0x1000000)
            ? XBE_XOR_KT_DEBUG
            : XBE_XOR_KT_RETAIL;

    printf("xbe entry: 0x%x | thunk table: 0x%x\n", xbeh->dwEntryAddr, xbeh->dwKernelImageThunkAddr);

    {
        XbeSectionHeader *xbes = (void *)xbeh->dwSectionHeadersAddr;
        uint32_t *ktt = (void *)xbeh->dwKernelImageThunkAddr;
        uint32_t t;
        size_t ctr;

        for (ctr = 0; ctr < xbeh->dwSections; ++ctr) {
            if ((uint32_t)ktt < xbes[ctr].dwVirtualAddr ||
                (uint32_t)ktt >= xbes[ctr].dwVirtualAddr + xbes[ctr].dwVirtualSize) {
                continue;
            }

            printf("patching kernel thunk table at address %p section %u\n", ktt, ctr);

            for (ctr = 0; ktt[ctr]; ++ctr) {
                t = ktt[ctr] & 0x7fffffff;
                ktt[ctr] = _CxbxKrnl_KernelThunkTable()[t];
                printf("thunk %3u: %p <- 0x%.08x\n", t, &ktt[ctr], ktt[ctr]);
            }
            break;
        }
    }

    CxbxKrnlInit(
        hwnd,                                       /* hwndParent */
        (void *)xbeh->dwTLSAddr + sizeof(XbeTLS),   /* pTLSData */
        (void *)xbeh->dwTLSAddr,                    /* pTLS */
        (void *)xbeh->dwLibraryVersionsAddr,        /* pLibraryVersion */
        (argc >= 4) ? atoi(argv[3]) : 1,            /* DbgMode */
        (argc >= 5) ? argv[4] : "",                 /* szDebugFilename */
        xbeh,                                       /* pXbeHeader */
        xbeh->dwSizeofHeaders,                      /* dwXbeHeaderSize */
        (void *)xbeh->dwEntryAddr);                 /* Entry */

    munmap((void *)XBE_BASE_ADDR, start - XBE_BASE_ADDR);

    return 0;
}


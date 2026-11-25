/*
 * The game's DLLs (docs/recon.md, docs/status.md): most of its code is in 11 SN Systems DLLs that SYS_DLLMain
 * reads from the AFS into the dll_buf pool, at an address the game's FIFO allocator chooses. They were
 * translated ahead of time (tools/recomp/recomp.py --dll) with every address as an offset from the DLL's base.
 *
 * snDllLoaded(image, out) (SN's loader, 0x132318) relocates the image where it lies, links it, then runs its
 * constructors. Hooked before it runs: the image is still as it came off the disc, so its size and CRC say which
 * DLL it is; the translated functions are registered at that base (rt_dll_register) before any of its code runs.
 * The loader itself then runs as it is: the data is relocated in guest memory, and the translated code reads its
 * relocated immediates back from there.
 */
#include <stdio.h>
#include <string.h>

#include "r5900.h"
#include "runtime.h"

extern const DllModule *const dll_modules[];   /* build/dll/dlls.c */
extern const int dll_module_count;

static u32 crc32_of(Ctx *c, u32 addr, u32 size) {
    static u32 table[256];
    if (!table[1])
        for (u32 i = 0; i < 256; i++) {
            u32 v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < size; i++) crc = table[(crc ^ rd8(c, addr + i)) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void hle_snDllLoaded(Ctx *c) {
    u32 image = RW(4);
    if (rd32(c, image) != 0x31524E53u) {           /* "SNR1" */
        rt_log("dll", "snDllLoaded(%08X): not an SNR1 image", image);
        return;
    }
    u32 size = rd32(c, image + 0x20), crc = crc32_of(c, image, size);
    for (int i = 0; i < dll_module_count; i++) {
        const DllModule *m = dll_modules[i];
        if (m->size == size && m->crc == crc) {
            rt_dll_register(image, m);
            rt_log("dll", "%s loaded at %08X (%u functions)", m->name, image, m->count);
            return;
        }
    }
    rt_log("dll", "snDllLoaded(%08X): an unknown DLL (size %X, crc %08X): its code was not translated", image, size, crc);
}

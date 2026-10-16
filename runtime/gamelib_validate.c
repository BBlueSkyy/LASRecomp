#define _POSIX_C_SOURCE 200809L
#include "gamelib_validate.h"
#include "address_layout.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

static bool invalid(char *why, size_t n, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    if (why && n) vsnprintf(why, n, format, ap);
    va_end(ap);
    return false;
}

static bool table(const uint64_t *addrs, const GuestFn *fns, const uint64_t *count,
                  uint64_t lo, uint64_t hi, GameLibRange range, void *context,
                  char *why, size_t size) {
    if (!count || (uintptr_t)count % _Alignof(uint64_t) ||
        !range(count, sizeof *count, false, context))
        return invalid(why, size, "game module has an invalid count pointer");
    uint64_t n = *count;
    // Every guest instruction is four bytes. This also bounds all size products.
    if (!n || n > (hi - lo) / 4 || n > SIZE_MAX / sizeof *fns ||
        n > SIZE_MAX / sizeof *addrs)
        return invalid(why, size, "game module has an invalid function count");
    if (!addrs || !fns || (uintptr_t)addrs % _Alignof(uint64_t) ||
        (uintptr_t)fns % _Alignof(GuestFn) ||
        !range(addrs, (size_t)n * sizeof *addrs, false, context) ||
        !range(fns, (size_t)n * sizeof *fns, false, context))
        return invalid(why, size, "game module function tables are outside its mapped image");
    for (uint64_t i = 0; i < n; ++i) {
        if (addrs[i] < lo || addrs[i] >= hi || (addrs[i] & 3) ||
            (i && addrs[i] <= addrs[i - 1]))
            return invalid(why, size, "game module guest addresses are invalid or unsorted");
        if (!fns[i] || !range((const void *)(uintptr_t)fns[i], 1, true, context))
            return invalid(why, size, "game module function pointer is outside executable code");
    }
    return true;
}

bool gamelib_validate_tables(const GameLibTables *t, GameLibRange range, void *context,
                             char *why, size_t size) {
    if (!range || !t || (uintptr_t)t % _Alignof(GameLibTables) ||
        !range(t, sizeof *t, false, context))
        return invalid(why, size, "game module returned an invalid table pointer");
    if (t->abi != GAMELIB_ABI)
        return invalid(why, size, "game module ABI does not match this runtime");
    if (!t->id || !range(t->id, sizeof GAMELIB_ID, false, context) ||
        memcmp(t->id, GAMELIB_ID, sizeof GAMELIB_ID))
        return invalid(why, size, "game module was built for another runtime version");
    return table(t->main_addrs, t->main_fns, t->main_count, MAIN_BASE, SDK_BASE,
                 range, context, why, size) &&
           table(t->sdk_addrs, t->sdk_fns, t->sdk_count, SDK_BASE, SDK_BASE + 0x4000000ull,
                 range, context, why, size);
}

#if defined(__linux__) || defined(__ANDROID__)
#include <elf.h>
#include <sys/stat.h>
#include <unistd.h>

static bool within(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

static bool read_at(FILE *f, uint64_t offset, void *out, size_t n) {
    return offset <= INT64_MAX && fseeko(f, (off_t)offset, SEEK_SET) == 0 && fread(out, 1, n, f) == n;
}

bool gamelib_validate_elf(const char *path, unsigned machine, char *why, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) return invalid(why, size, "cannot open game module");
    struct stat st;
    Elf64_Ehdr h;
    const char *error = "game module is not a valid ELF64 shared library";
    bool ok = false, entry = false, load = false, dynamic = false;
    if (fstat(fileno(f), &st) || st.st_size < (off_t)sizeof h || !read_at(f, 0, &h, sizeof h)) goto done;
    uint64_t length = (uint64_t)st.st_size;
    if (memcmp(h.e_ident, ELFMAG, SELFMAG) || h.e_ident[EI_CLASS] != ELFCLASS64 ||
        h.e_ident[EI_DATA] != ELFDATA2LSB || h.e_ident[EI_VERSION] != EV_CURRENT ||
        h.e_version != EV_CURRENT || h.e_type != ET_DYN || h.e_ehsize != sizeof h) goto done;
    error = "game module CPU architecture does not match this runtime";
    if (h.e_machine != machine) goto done;
    error = "game module has invalid ELF program headers";
    if (!h.e_phnum || h.e_phnum == PN_XNUM || h.e_phentsize != sizeof(Elf64_Phdr) ||
        !within(h.e_phoff, (uint64_t)h.e_phnum * sizeof(Elf64_Phdr), length)) goto done;
    for (unsigned i = 0; i < h.e_phnum; ++i) {
        Elf64_Phdr p;
        if (!read_at(f, h.e_phoff + i * sizeof p, &p, sizeof p) ||
            !within(p.p_offset, p.p_filesz, length)) goto done;
        if (p.p_type == PT_INTERP) { error = "game module is an executable, not a shared library"; goto done; }
        if (p.p_type == PT_GNU_STACK && (p.p_flags & PF_X)) {
            error = "game module requests an executable stack"; goto done;
        }
        if (p.p_type == PT_LOAD) {
            load = true;
            if (p.p_filesz > p.p_memsz || p.p_vaddr > UINT64_MAX - p.p_memsz ||
                (p.p_flags & (PF_W | PF_X)) == (PF_W | PF_X) ||
                (p.p_align > 1 && ((p.p_align & (p.p_align - 1)) ||
                 p.p_vaddr % p.p_align != p.p_offset % p.p_align))) goto done;
        }
        if (p.p_type == PT_DYNAMIC) {
            dynamic = true;
            bool terminated = false;
            if (p.p_filesz % sizeof(Elf64_Dyn)) goto done;
            for (uint64_t j = 0; j < p.p_filesz / sizeof(Elf64_Dyn); ++j) {
                Elf64_Dyn d;
                if (!read_at(f, p.p_offset + j * sizeof d, &d, sizeof d)) goto done;
                if (d.d_tag == DT_NULL) { terminated = true; break; }
                if (d.d_tag == DT_TEXTREL || (d.d_tag == DT_FLAGS && (d.d_un.d_val & DF_TEXTREL))) {
                    error = "game module contains text relocations"; goto done;
                }
            }
            if (!terminated) goto done;
        }
    }
    if (!load || !dynamic) goto done;
    error = "game module lacks a valid exported lasgame_tables function";
    // Normal NDK-produced .so files retain .dynsym even when stripped. Require it
    // so the entry can be checked without executing constructors or module code.
    if (!h.e_shnum || h.e_shentsize != sizeof(Elf64_Shdr) ||
        !within(h.e_shoff, (uint64_t)h.e_shnum * sizeof(Elf64_Shdr), length)) goto done;
    for (unsigned i = 0; i < h.e_shnum; ++i) {
        Elf64_Shdr s, strings;
        if (!read_at(f, h.e_shoff + i * sizeof s, &s, sizeof s)) goto done;
        if (s.sh_type != SHT_DYNSYM) continue;
        if (s.sh_entsize != sizeof(Elf64_Sym) || s.sh_size % sizeof(Elf64_Sym) ||
            s.sh_link >= h.e_shnum || !within(s.sh_offset, s.sh_size, length) ||
            !read_at(f, h.e_shoff + s.sh_link * sizeof strings, &strings, sizeof strings) ||
            strings.sh_type != SHT_STRTAB || !within(strings.sh_offset, strings.sh_size, length)) goto done;
        for (uint64_t j = 0; j < s.sh_size / sizeof(Elf64_Sym); ++j) {
            Elf64_Sym sym;
            char name[sizeof GAMELIB_ENTRY];
            if (!read_at(f, s.sh_offset + j * sizeof sym, &sym, sizeof sym)) goto done;
            if (sym.st_shndx == SHN_UNDEF || ELF64_ST_TYPE(sym.st_info) != STT_FUNC ||
                ELF64_ST_BIND(sym.st_info) != STB_GLOBAL ||
                (sym.st_other & 3) != STV_DEFAULT ||
                !within(sym.st_name, sizeof name, strings.sh_size)) continue;
            if (!read_at(f, strings.sh_offset + sym.st_name, name, sizeof name)) goto done;
            if (!memcmp(name, GAMELIB_ENTRY, sizeof name)) entry = true;
        }
    }
    ok = entry;
done:
    fclose(f);
    if (!ok) return invalid(why, size, "%s", error);
    return true;
}
#else
bool gamelib_validate_elf(const char *path, unsigned machine, char *why, size_t size) {
    (void)path; (void)machine;
    return invalid(why, size, "ELF validation is unavailable on this platform");
}
#endif

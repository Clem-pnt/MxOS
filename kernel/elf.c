#include "elf.h"
#include "screen.h"

// Sous-ensemble minimal du format ELF32, suffisant pour charger un petit
// exécutable i386 (pas de bibliothèque standard <elf.h> en freestanding).
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} __attribute__((packed)) Elf32_Shdr;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} __attribute__((packed)) Elf32_Rel;

#define PT_LOAD   1
#define SHT_REL   9
#define R_386_32   1
#define R_386_PC32 2

static int in_window(uint32_t off, uint32_t len, uint32_t window_size) {
    if (len > window_size) return 0;
    if (off > window_size - len) return 0;
    return 1;
}

int elf_load(const uint8_t *image, uint32_t image_size,
             uint32_t load_base, uint32_t window_size,
             uint32_t link_base, uint32_t *out_entry) {
    if (image_size < sizeof(Elf32_Ehdr)) {
        kprint("ELF invalide : fichier trop court.\n");
        return 0;
    }

    const Elf32_Ehdr *eh = (const Elf32_Ehdr*)image;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != 1 /* ELFCLASS32 */ ||
        eh->e_machine != 3 /* EM_386 */) {
        kprint("ELF invalide : signature/format inattendu.\n");
        return 0;
    }

    int32_t delta = (int32_t)load_base - (int32_t)link_base;

    // 1) Segments PT_LOAD : recopie filesz octets depuis le fichier, puis
    // met à zéro le reste (memsz - filesz, typiquement le .bss).
    if (eh->e_phoff + (uint32_t)eh->e_phnum * sizeof(Elf32_Phdr) > image_size) {
        kprint("ELF invalide : table de programme hors fichier.\n");
        return 0;
    }
    const Elf32_Phdr *ph = (const Elf32_Phdr*)(image + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;

        uint32_t dest_off = ph[i].p_vaddr - link_base;
        if (!in_window(dest_off, ph[i].p_memsz, window_size)) {
            kprint("ELF invalide : segment PT_LOAD hors de la fenetre memoire.\n");
            return 0;
        }
        if (ph[i].p_offset + ph[i].p_filesz > image_size) {
            kprint("ELF invalide : segment PT_LOAD hors du fichier.\n");
            return 0;
        }

        uint8_t *dest = (uint8_t*)(load_base + dest_off);
        const uint8_t *src = image + ph[i].p_offset;
        for (uint32_t k = 0; k < ph[i].p_filesz; k++) dest[k] = src[k];
        for (uint32_t k = ph[i].p_filesz; k < ph[i].p_memsz; k++) dest[k] = 0;
    }

    // 2) Relocations R_386_32 (adresses absolues) : corrigées de `delta` si
    // le programme est chargé à une adresse différente de son adresse de
    // lien (cf. commentaire de kernel/elf.h). Les sections de relocations
    // (SHT_REL) ne sont conservées dans le fichier final que grâce à
    // `--emit-relocs` passé à `ld` (cf. Makefile) : un exécutable ELF
    // normal ne les garde pas, puisqu'il n'est censé être chargé qu'à son
    // adresse de lien fixe.
    if (eh->e_shoff != 0 &&
        eh->e_shoff + (uint32_t)eh->e_shnum * sizeof(Elf32_Shdr) <= image_size) {
        const Elf32_Shdr *sh = (const Elf32_Shdr*)(image + eh->e_shoff);
        for (int s = 0; s < eh->e_shnum; s++) {
            if (sh[s].sh_type != SHT_REL) continue;
            if (sh[s].sh_offset + sh[s].sh_size > image_size) continue;

            uint32_t rel_count = sh[s].sh_entsize ? sh[s].sh_size / sh[s].sh_entsize : 0;
            const Elf32_Rel *rel = (const Elf32_Rel*)(image + sh[s].sh_offset);

            for (uint32_t r = 0; r < rel_count; r++) {
                uint32_t type = rel[r].r_info & 0xFF;
                if (type != R_386_32) continue; // R_386_PC32 et autres : rien à faire

                uint32_t dest_off = rel[r].r_offset - link_base;
                if (!in_window(dest_off, 4, window_size)) continue;

                uint32_t *word = (uint32_t*)(load_base + dest_off);
                *word = (uint32_t)((int32_t)*word + delta);
            }
        }
    }

    if (out_entry) *out_entry = load_base + (eh->e_entry - link_base);
    return 1;
}

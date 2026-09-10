#ifndef ELF_H
#define ELF_H

#include <stdint.h>

// Charge un exécutable ELF32 i386 (produit par `ld ... --emit-relocs`,
// cf. Makefile) depuis `image` (en mémoire, `image_size` octets) vers la
// fenêtre mémoire [load_base, load_base+window_size).
//
// Contrairement à un simple binaire plat, le fichier reste un vrai ELF avec
// ses en-têtes de programme (PT_LOAD) : chaque segment est recopié à sa
// place, et le .bss (memsz > filesz) est mis à zéro. Le binaire est lié une
// seule fois pour l'adresse `link_base` (cf. userprogs/user.ld), mais grâce
// aux relocations R_386_32 conservées par --emit-relocs, ce même fichier
// peut être chargé à une AUTRE adresse (`load_base != link_base`, ex: les
// slots 1-3 de kernel/exec.c) : chaque adresse absolue embarquée dans le
// code/les données est corrigée en lui ajoutant le décalage
// `load_base - link_base`. Les relocations R_386_PC32 (appels/sauts
// relatifs) n'ont pas besoin d'être touchées : un décalage uniforme de tout
// le programme ne change pas une différence entre deux adresses internes.
//
// Renvoie 1 en cas de succès (et écrit le point d'entrée absolu dans
// *out_entry), 0 en cas d'erreur (format invalide, segment hors fenêtre...).
int elf_load(const uint8_t *image, uint32_t image_size,
             uint32_t load_base, uint32_t window_size,
             uint32_t link_base, uint32_t *out_entry);

#endif

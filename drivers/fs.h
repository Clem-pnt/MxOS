#ifndef FS_H
#define FS_H

#include <stdint.h>

typedef struct {
    char name[24];      // Nom du fichier (zéro-terminé)
    uint32_t start_lba; // Secteur de début
    uint32_t size_sect; // Taille en secteurs
} __attribute__((packed)) FileEntry;

// LBA de départ du répertoire racine : juste après le secteur de boot (1)
// et les KERNEL_SECTORS secteurs du noyau (cf. Makefile/boot.asm -
// actuellement 80, doit rester synchronisé avec ce +1). Un embarquement de
// programmes utilisateur plus gros dans le noyau nécessitera d'augmenter
// à la fois KERNEL_SECTORS (Makefile + boot.asm) ET cette constante en
// conséquence, pour ne jamais faire chevaucher le noyau et le système de
// fichiers sur le disque.
#define FS_ROOT_LBA 81
#define FS_MAX_FILES 32 // Etait 15 (limite a 1 seul secteur) ; desormais etale sur plusieurs secteurs (cf. FS_ROOT_SECTORS)
// Secteur dédié à la bitmap d'allocation des blocs de données (1 octet par
// secteur, 0 = libre / 1 = utilisé) : remplace l'ancien allocateur "bump"
// (next_free_lba) qui ne récupérait jamais l'espace libéré par fs_delete_file.
// Placée juste après les FS_ROOT_SECTORS secteurs qu'occupe RootDirectory
// (cf. fs.c : FS_ROOT_SECTORS = ceil(sizeof(RootDirectory)/512)).
#define FS_BITMAP_LBA (FS_ROOT_LBA + 4) // 4 secteurs de marge, cf. static_assert dans fs.c
// Zone de données de fichiers proprement dite (après le secteur bitmap).
#define FS_DATA_LBA (FS_BITMAP_LBA + 1)
// Réserve 512 secteurs (256 Ko, bitmap incluse) de zone de données après la
// table racine ; la bitmap n'en décrit donc que FS_DATA_SECTORS-1.
#define FS_DATA_SECTORS 512
#define FS_DATA_END_LBA (FS_BITMAP_LBA + FS_DATA_SECTORS)
#define FS_MAGIC 0x3153464D

typedef struct {
    uint32_t magic;
    FileEntry files[FS_MAX_FILES];
    uint32_t next_free_lba; // Obsolete (ancien allocateur bump), conserve pour compatibilite de mise en page sur disque, non utilise.
    uint8_t reserved[24];
} __attribute__((packed)) RootDirectory;

void fs_init();
void fs_list();
void fs_read_file(char* name);
int fs_file_exists(char* name);
int fs_load_file(char* name, uint8_t* dest, uint32_t max_size, uint32_t *out_size);
void fs_create_file(char* name, uint32_t start_lba, uint32_t size_sect);
int fs_delete_file(char* name);
int fs_delete_file_silent(char* name); // Comme fs_delete_file, sans messages (config/historique)
int fs_write_file(char* name, const uint8_t* data, uint32_t size_bytes);
int fs_write_file_silent(char* name, const uint8_t* data, uint32_t size_bytes); // Idem, sans messages
#endif
#ifndef FS_H
#define FS_H

#include <stdint.h>

typedef struct {
    char name[24];      // Nom du fichier (zéro-terminé)
    uint32_t start_lba; // Secteur de début
    uint32_t size_sect; // Taille en secteurs
} __attribute__((packed)) FileEntry;

#define FS_ROOT_LBA 64
#define FS_DATA_LBA 65
// Réserve 512 secteurs (256 Ko) de zone de données après la table racine.
#define FS_DATA_SECTORS 512
#define FS_DATA_END_LBA (FS_DATA_LBA + FS_DATA_SECTORS)
#define FS_MAGIC 0x3153464D

typedef struct {
    uint32_t magic;
    FileEntry files[15];
    uint32_t next_free_lba; // Allocateur "bump" pour les données de fichiers
    uint8_t reserved[24];
} __attribute__((packed)) RootDirectory;

void fs_init();
void fs_list();
void fs_read_file(char* name);
int fs_file_exists(char* name);
int fs_load_file(char* name, uint8_t* dest, uint32_t max_size, uint32_t *out_size);
void fs_create_file(char* name, uint32_t start_lba, uint32_t size_sect);
int fs_delete_file(char* name);
int fs_write_file(char* name, const uint8_t* data, uint32_t size_bytes);
#endif
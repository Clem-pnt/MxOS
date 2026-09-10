#include "fs.h"
#include "ata.h"
#include "../kernel/screen.h"

static int m_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int valid_name(const char *name) {
    for (int i = 0; i < 24; i++) {
        if (name[i] == 0) return i > 0;
    }
    return 0;
}

static void print_text_buffer(const uint8_t* data, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        char c = data[i];
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 32 && c < 127)) {
            kprint_char(c);
        } else {
            kprint_char('.');
        }
    }
}

static RootDirectory root;

void fs_init() {
    if (!ata_read_sector(FS_ROOT_LBA, (uint16_t*)&root) ||
        root.magic != FS_MAGIC) {
        for (uint32_t i = 0; i < sizeof(root); i++) {
            ((uint8_t*)&root)[i] = 0;
        }
        root.magic = FS_MAGIC;
        root.next_free_lba = FS_DATA_LBA;
    }
    // Protège contre une valeur corrompue lue depuis le disque
    if (root.next_free_lba < FS_DATA_LBA || root.next_free_lba > FS_DATA_END_LBA) {
        root.next_free_lba = FS_DATA_LBA;
    }
}

void fs_list() {
    kprint("Fichiers sur MxOS :\n");
    for(int i = 0; i < 15; i++) {
        if (valid_name(root.files[i].name)) {
            kprint("- ");
            kprint(root.files[i].name);
            kprint("\n");
        }
    }
}

void fs_read_file(char* name) {
    uint8_t buffer[512];
    if (!name || name[0] == 0) {
        kprint("Nom de fichier invalide.\n");
        return;
    }
    for (int i = 0; i < 15; i++) {
        if (valid_name(root.files[i].name) && m_strcmp(root.files[i].name, name) == 0) {
            kprint("Lecture de : ");
            kprint(name);
            kprint("\n");
            uint32_t total_bytes = root.files[i].size_sect * 512;
            uint32_t remaining = total_bytes;
            uint32_t lba = root.files[i].start_lba;

            if (lba < FS_DATA_LBA ||
                root.files[i].size_sect == 0 ||
                root.files[i].size_sect > 0x100000 ||
                lba > 0xFFFFFFFFu - root.files[i].size_sect) {
                kprint("Metadonnees de fichier invalides.\n");
                return;
            }
            while (remaining > 0) {
                if (!ata_read_sector(lba++, (uint16_t*)buffer)) {
                    kprint("Erreur de lecture disque.\n");
                    return;
                }
                uint32_t chunk = remaining > 512 ? 512 : remaining;
                print_text_buffer(buffer, chunk);
                remaining -= chunk;
            }
            kprint("\n");
            return;
        }
    }
    kprint("Fichier introuvable : ");
    kprint(name);
    kprint("\n");
}

// Enregistre une entrée de répertoire (nom + emplacement) puis persiste la
// table racine sur le disque. Utilisé par fs_create_file et fs_write_file.
static int fs_add_entry(char* name, uint32_t start_lba, uint32_t size_sect) {
    for (int i = 0; i < 15; i++) {
        if (root.files[i].name[0] == 0) {
            for (int j = 0; j < 23; j++) {
                root.files[i].name[j] = name[j];
                if (name[j] == 0) break;
            }
            root.files[i].name[23] = 0;
            root.files[i].start_lba = start_lba;
            root.files[i].size_sect = size_sect;

            if (!ata_write_sector(FS_ROOT_LBA, (uint16_t*)&root)) {
                kprint("Erreur d'ecriture disque.\n");
                return 0;
            }
            return 1;
        }
    }
    kprint("Erreur : Root Directory plein !\n");
    return 0;
}

void fs_create_file(char* name, uint32_t start_lba, uint32_t size_sect) {
    if (!name || name[0] == 0 || start_lba < FS_DATA_LBA || size_sect == 0) {
        kprint("Metadonnees de fichier invalides.\n");
        return;
    }
    if (fs_add_entry(name, start_lba, size_sect)) {
        kprint("Fichier cree avec succes.\n");
    }
}

// Écrit réellement le contenu `data` (size_bytes octets) sur le disque, en
// allouant l'espace via l'allocateur "bump" next_free_lba, puis enregistre
// l'entrée correspondante. Retourne 1 en cas de succès, 0 sinon.
int fs_write_file(char* name, const uint8_t* data, uint32_t size_bytes) {
    if (!name || name[0] == 0 || !data || size_bytes == 0) {
        kprint("Parametres invalides.\n");
        return 0;
    }

    uint32_t size_sect = (size_bytes + 511) / 512;
    if (root.next_free_lba + size_sect > FS_DATA_END_LBA) {
        kprint("Espace disque insuffisant.\n");
        return 0;
    }

    uint32_t start_lba = root.next_free_lba;
    uint32_t lba = start_lba;
    uint32_t remaining = size_bytes;
    uint8_t buffer[512];

    while (remaining > 0) {
        uint32_t chunk = remaining > 512 ? 512 : remaining;
        for (uint32_t i = 0; i < 512; i++) {
            buffer[i] = (i < chunk) ? data[i] : 0;
        }
        if (!ata_write_sector(lba, (uint16_t*)buffer)) {
            kprint("Erreur d'ecriture disque.\n");
            return 0;
        }
        data += chunk;
        remaining -= chunk;
        lba++;
    }

    root.next_free_lba = start_lba + size_sect;
    if (!fs_add_entry(name, start_lba, size_sect)) {
        return 0;
    }
    kprint("Fichier ecrit avec succes.\n");
    return 1;
}
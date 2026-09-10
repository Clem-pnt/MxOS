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
// Nombre de secteurs de 512 octets nécessaires pour stocker RootDirectory
// en entier (avec FS_MAX_FILES entrées) : le répertoire racine ne tient
// plus forcément dans un seul secteur comme à l'origine (15 entrées max).
#define FS_ROOT_SECTORS ((sizeof(RootDirectory) + 511) / 512)

// Charge RootDirectory depuis le disque (FS_ROOT_SECTORS secteurs
// consécutifs à partir de FS_ROOT_LBA) dans `root`. Renvoie 1 si toutes les
// lectures ont réussi, 0 sinon (auquel cas le contenu de `root` ne doit pas
// être considéré fiable).
static int fs_root_load(void) {
    uint8_t *raw = (uint8_t*)&root;
    uint8_t sector_buf[512];
    for (uint32_t s = 0; s < FS_ROOT_SECTORS; s++) {
        if (!ata_read_sector(FS_ROOT_LBA + s, (uint16_t*)sector_buf)) return 0;
        uint32_t offset = s * 512;
        uint32_t remaining = (uint32_t)sizeof(RootDirectory) - offset;
        uint32_t chunk = remaining > 512 ? 512 : remaining;
        for (uint32_t i = 0; i < chunk; i++) raw[offset + i] = sector_buf[i];
    }
    return 1;
}

// Persiste `root` sur le disque (FS_ROOT_SECTORS secteurs consécutifs à
// partir de FS_ROOT_LBA). Renvoie 1 si toutes les écritures ont réussi.
static int fs_root_save(void) {
    uint8_t *raw = (uint8_t*)&root;
    uint8_t sector_buf[512];
    for (uint32_t s = 0; s < FS_ROOT_SECTORS; s++) {
        uint32_t offset = s * 512;
        uint32_t remaining = (uint32_t)sizeof(RootDirectory) - offset;
        uint32_t chunk = remaining > 512 ? 512 : remaining;
        for (uint32_t i = 0; i < 512; i++) sector_buf[i] = (i < chunk) ? raw[offset + i] : 0;
        if (!ata_write_sector(FS_ROOT_LBA + s, (uint16_t*)sector_buf)) return 0;
    }
    return 1;
}

// Bitmap des secteurs de la zone de données : 1 octet par secteur (0=libre,
// 1=utilisé), indexée à partir de FS_DATA_LBA. Un octet par bit serait plus
// compact, mais le secteur bitmap (512 octets) suffit largement pour
// FS_DATA_SECTORS-1 entrées et le code reste plus simple/robuste ainsi.
static uint8_t bitmap[512];

static void bitmap_persist(void) {
    ata_write_sector(FS_BITMAP_LBA, (uint16_t*)bitmap);
}

// Cherche `count` secteurs de données CONSECUTIFS libres dans la bitmap.
// Renvoie le LBA de début, ou 0 si aucun espace suffisant n'est disponible.
static uint32_t bitmap_find_free_run(uint32_t count) {
    uint32_t run_start = 0;
    uint32_t run_len = 0;

    for (uint32_t i = 0; i < FS_DATA_SECTORS - 1; i++) {
        if (bitmap[i] == 0) {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len >= count) {
                return FS_DATA_LBA + run_start;
            }
        } else {
            run_len = 0;
        }
    }
    return 0;
}

static void bitmap_mark(uint32_t start_lba, uint32_t count, uint8_t used) {
    if (start_lba < FS_DATA_LBA) return;
    uint32_t idx = start_lba - FS_DATA_LBA;
    for (uint32_t i = 0; i < count && idx + i < FS_DATA_SECTORS - 1; i++) {
        bitmap[idx + i] = used;
    }
    bitmap_persist();
}

void fs_init() {
    if (!fs_root_load() ||
        root.magic != FS_MAGIC) {
        for (uint32_t i = 0; i < sizeof(root); i++) {
            ((uint8_t*)&root)[i] = 0;
        }
        root.magic = FS_MAGIC;
        root.next_free_lba = FS_DATA_LBA;
        for (uint32_t i = 0; i < sizeof(bitmap); i++) bitmap[i] = 0;
        bitmap_persist();
        fs_root_save();
    } else if (!ata_read_sector(FS_BITMAP_LBA, (uint16_t*)bitmap)) {
        for (uint32_t i = 0; i < sizeof(bitmap); i++) bitmap[i] = 0;
    }
}

void fs_list() {
    kprint("Fichiers sur MxOS :\n");
    for(int i = 0; i < FS_MAX_FILES; i++) {
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
    for (int i = 0; i < FS_MAX_FILES; i++) {
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

int fs_file_exists(char* name) {
    if (!name || name[0] == 0) return 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (valid_name(root.files[i].name) && m_strcmp(root.files[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Charge le contenu brut d'un fichier dans `dest` (jusqu'à `max_size` octets),
// utilisé par le chargeur `exec` (kernel/exec.c) pour amener un programme
// utilisateur en mémoire avant de créer sa tâche ring3. Contrairement à
// fs_read_file (qui filtre/affiche du texte), copie les octets tels quels :
// le fichier est traité comme un binaire plat exécutable.
int fs_load_file(char* name, uint8_t* dest, uint32_t max_size, uint32_t *out_size) {
    if (!name || name[0] == 0 || !dest) {
        kprint("Parametres invalides.\n");
        return 0;
    }
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (valid_name(root.files[i].name) && m_strcmp(root.files[i].name, name) == 0) {
            uint32_t total_bytes = root.files[i].size_sect * 512;
            uint32_t lba = root.files[i].start_lba;

            if (lba < FS_DATA_LBA ||
                root.files[i].size_sect == 0 ||
                root.files[i].size_sect > 0x100000 ||
                lba > 0xFFFFFFFFu - root.files[i].size_sect) {
                kprint("Metadonnees de fichier invalides.\n");
                return 0;
            }
            if (total_bytes > max_size) {
                kprint("Programme trop volumineux pour la zone de chargement.\n");
                return 0;
            }

            uint32_t remaining = total_bytes;
            uint8_t *out = dest;
            uint8_t buffer[512];
            while (remaining > 0) {
                if (!ata_read_sector(lba++, (uint16_t*)buffer)) {
                    kprint("Erreur de lecture disque.\n");
                    return 0;
                }
                uint32_t chunk = remaining > 512 ? 512 : remaining;
                for (uint32_t k = 0; k < chunk; k++) out[k] = buffer[k];
                out += chunk;
                remaining -= chunk;
            }
            if (out_size) *out_size = total_bytes;
            return 1;
        }
    }
    kprint("Fichier introuvable : ");
    kprint(name);
    kprint("\n");
    return 0;
}

// Enregistre une entrée de répertoire (nom + emplacement) puis persiste la
// table racine sur le disque. Utilisé par fs_create_file et fs_write_file.
static int fs_add_entry(char* name, uint32_t start_lba, uint32_t size_sect) {
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (root.files[i].name[0] == 0) {
            for (int j = 0; j < 23; j++) {
                root.files[i].name[j] = name[j];
                if (name[j] == 0) break;
            }
            root.files[i].name[23] = 0;
            root.files[i].start_lba = start_lba;
            root.files[i].size_sect = size_sect;

            if (!fs_root_save()) {
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

// Supprime l'entrée de répertoire correspondant à `name` ET libère
// réellement les secteurs qu'il occupait dans la bitmap (contrairement à
// l'ancien allocateur "bump" qui ne récupérait jamais cet espace). Persiste
// la table racine mise à jour sur le disque. Renvoie 1 si le fichier a été
// supprimé.
// Supprime l'entrée de répertoire correspondant à `name` ET libère
// réellement les secteurs qu'il occupait dans la bitmap (contrairement à
// l'ancien allocateur "bump" qui ne récupérait jamais cet espace). Persiste
// la table racine mise à jour sur le disque. Renvoie 1 si le fichier a été
// supprimé. `verbose` contrôle l'affichage des messages de statut/erreur :
// utilisé à 0 par fs_delete_file_silent() pour la persistance de config
// (kernel/shell.c), qui ne doit pas polluer la sortie shell à chaque commande.
static int fs_delete_file_impl(char* name, int verbose) {
    if (!name || name[0] == 0) {
        if (verbose) kprint("Nom de fichier invalide.\n");
        return 0;
    }
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (valid_name(root.files[i].name) && m_strcmp(root.files[i].name, name) == 0) {
            bitmap_mark(root.files[i].start_lba, root.files[i].size_sect, 0);

            for (int j = 0; j < 24; j++) root.files[i].name[j] = 0;
            root.files[i].start_lba = 0;
            root.files[i].size_sect = 0;

            if (!fs_root_save()) {
                if (verbose) kprint("Erreur d'ecriture disque.\n");
                return 0;
            }
            if (verbose) kprint("Fichier supprime.\n");
            return 1;
        }
    }
    if (verbose) {
        kprint("Fichier introuvable : ");
        kprint(name);
        kprint("\n");
    }
    return 0;
}

int fs_delete_file(char* name) {
    return fs_delete_file_impl(name, 1);
}

int fs_delete_file_silent(char* name) {
    return fs_delete_file_impl(name, 0);
}

// Écrit réellement le contenu `data` (size_bytes octets) sur le disque, en
// allouant l'espace via la bitmap de blocs libres (bitmap_find_free_run),
// ce qui permet de réutiliser l'espace libéré par un fs_delete_file
// précédent, puis enregistre l'entrée correspondante. Retourne 1 en cas de
// succès, 0 sinon.
// Écrit réellement le contenu `data` (size_bytes octets) sur le disque, en
// allouant l'espace via la bitmap de blocs libres (bitmap_find_free_run),
// ce qui permet de réutiliser l'espace libéré par un fs_delete_file
// précédent, puis enregistre l'entrée correspondante. Retourne 1 en cas de
// succès, 0 sinon. `verbose` contrôle l'affichage des messages (cf.
// fs_delete_file_impl : fs_write_file_silent() l'utilise à 0 pour la
// persistance de config/historique, kernel/shell.c).
static int fs_write_file_impl(char* name, const uint8_t* data, uint32_t size_bytes, int verbose) {
    if (!name || name[0] == 0 || !data || size_bytes == 0) {
        if (verbose) kprint("Parametres invalides.\n");
        return 0;
    }

    // Si un fichier du même nom existe déjà, on le supprime d'abord : sans
    // cela, fs_add_entry() ajouterait une DEUXIÈME entrée dans la table
    // racine (elle ne cherche qu'un slot libre, sans vérifier les doublons),
    // et toute lecture ultérieure (fs_load_file/fs_read_file/...) continuerait
    // à trouver l'ANCIENNE entrée en premier (boucle i=0..14). C'est
    // indispensable pour la persistance de config/historique : history.dat
    // est réécrit après chaque commande, donc sans ce nettoyage la table
    // racine (15 entrées max) se remplirait en quelques commandes et
    // resterait bloquée sur le tout premier contenu enregistré.
    if (fs_file_exists(name)) {
        fs_delete_file_impl(name, 0);
    }

    uint32_t size_sect = (size_bytes + 511) / 512;
    uint32_t start_lba = bitmap_find_free_run(size_sect);
    if (start_lba == 0) {
        if (verbose) kprint("Espace disque insuffisant.\n");
        return 0;
    }

    uint32_t lba = start_lba;
    uint32_t remaining = size_bytes;
    uint8_t buffer[512];

    while (remaining > 0) {
        uint32_t chunk = remaining > 512 ? 512 : remaining;
        for (uint32_t i = 0; i < 512; i++) {
            buffer[i] = (i < chunk) ? data[i] : 0;
        }
        if (!ata_write_sector(lba, (uint16_t*)buffer)) {
            if (verbose) kprint("Erreur d'ecriture disque.\n");
            return 0;
        }
        data += chunk;
        remaining -= chunk;
        lba++;
    }

    bitmap_mark(start_lba, size_sect, 1);
    if (!fs_add_entry(name, start_lba, size_sect)) {
        bitmap_mark(start_lba, size_sect, 0); // annule l'allocation si l'entree ne peut pas etre enregistree
        return 0;
    }
    if (verbose) kprint("Fichier ecrit avec succes.\n");
    return 1;
}

int fs_write_file(char* name, const uint8_t* data, uint32_t size_bytes) {
    return fs_write_file_impl(name, data, size_bytes, 1);
}

int fs_write_file_silent(char* name, const uint8_t* data, uint32_t size_bytes) {
    return fs_write_file_impl(name, data, size_bytes, 0);
}
#include "ata.h"
#include "../kernel/cpu.h" // On remonte d'un cran pour chercher les fonctions I/O

// Attend que le disque ait fini son travail interne (BSY=0), sans exiger DRQ.
// Utilisé après l'émission de commandes qui ne transfèrent pas de données
// (ex: FLUSH CACHE), pour ne pas attendre indéfiniment un DRQ qui ne viendra jamais.
static int ata_wait_bsy_clear() {
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & 0x01) return 0; // Bit ERR
        if (!(status & 0x80)) return 1; // BSY retombé
    }
    return 0;
}

static int ata_wait_ready() {
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & 0x01) return 0;
        if (!(status & 0x80) && (status & 0x08)) return 1;
    }
    return 0;
}

// Version "single-shot" de la lecture d'un secteur (sans retry) : logique
// originale, renommée pour être enveloppée par ata_read_sector() ci-dessous.
static int ata_read_sector_once(uint32_t lba, uint16_t *buffer) {
    if (!ata_wait_bsy_clear()) return 0;

    // Sélection du drive (0xE0 = Master + LBA) et bits 24-27 de l'adresse
    outb(ATA_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LOW,  (uint8_t)lba);
    outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND,  ATA_CMD_READ);

    if (!ata_wait_ready()) return 0;

    // On lit 256 mots (16 bits) pour faire 512 octets
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_DATA);
    }
    return 1;
}

// Version "single-shot" de l'écriture d'un secteur (sans retry).
static int ata_write_sector_once(uint32_t lba, uint16_t *buffer) {
    if (!ata_wait_bsy_clear()) return 0;

    outb(ATA_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LOW,  (uint8_t)lba);
    outb(ATA_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND,  ATA_CMD_WRITE);

    if (!ata_wait_ready()) return 0;

    // On écrit les 512 octets (256 mots de 16 bits)

    for (int i = 0; i < 256; i++) {
        uint16_t data = buffer[i];
        __asm__ volatile("outw %w0, %w1" : : "a"(data), "Nd"(ATA_DATA));
    }

    // On force le disque à vider son cache (cette commande ne pose pas DRQ,
    // donc on attend seulement que BSY retombe).
    outb(ATA_COMMAND, 0xE7); // Cache Flush
    return ata_wait_bsy_clear();
}

// Nombre de tentatives avant d'abandonner une opération disque : un échec
// isolé (bruit électrique, contrôleur temporairement occupé, etc.) sur un
// disque virtuel/réel simple ne doit pas immédiatement faire échouer toute
// une écriture de fichier ; on retente quelques fois avant de renoncer.
#define ATA_MAX_RETRIES 3

int ata_read_sector(uint32_t lba, uint16_t *buffer) {
    for (int attempt = 0; attempt < ATA_MAX_RETRIES; attempt++) {
        if (ata_read_sector_once(lba, buffer)) return 1;
    }
    return 0;
}

int ata_write_sector(uint32_t lba, uint16_t *buffer) {
    for (int attempt = 0; attempt < ATA_MAX_RETRIES; attempt++) {
        if (ata_write_sector_once(lba, buffer)) return 1;
    }
    return 0;
}
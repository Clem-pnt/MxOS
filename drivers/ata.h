#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* --- PORTS DU CONTROLEUR PRIMAIRE --- */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE_SEL   0x1F6
#define ATA_COMMAND     0x1F7
#define ATA_STATUS      0x1F7

/* --- COMMANDES --- */
#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30

/* --- PROTOTYPES --- */
int ata_read_sector(uint32_t lba, uint16_t *buffer);
int ata_write_sector(uint32_t lba, uint16_t *buffer);

#endif
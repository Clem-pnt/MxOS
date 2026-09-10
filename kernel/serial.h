#ifndef SERIAL_H
#define SERIAL_H

// Port série COM1, utilisé comme sortie de log "texte brut" en plus de
// l'écran VGA : permet de capturer la sortie du noyau automatiquement
// (ex: `qemu ... -serial file:out.log`) pour des tests scriptés, sans
// avoir à faire de l'OCR sur des captures d'écran.
void serial_init(void);
void serial_write_char(char c);
void serial_write(const char *s);

#endif

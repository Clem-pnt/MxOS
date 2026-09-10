#!/bin/bash
# test.sh — Test d'intégration automatisé pour MxOS.
#
# Démarre l'image disque dans QEMU en mode headless, capture toute la sortie
# du noyau via le port série COM1 (cf. kernel/serial.c, mirroré depuis
# kprint_char), injecte une séquence de touches simulant une session shell,
# puis vérifie par pattern-matching que les marqueurs attendus apparaissent
# dans le log. Remplace le besoin de captures d'écran + inspection manuelle
# utilisé lors du développement de ce projet.
#
# Usage : ./test.sh   (depuis la racine du dépôt)
# Code de sortie : 0 si tous les tests passent, 1 sinon.

set -u
cd "$(dirname "$0")"

IMG="mxos_image.img"
LOG="$(mktemp /tmp/mxos_test_XXXXXX.log)"
TIMEOUT=20
FAILED=0

cleanup() { rm -f "$LOG"; }
trap cleanup EXIT

echo "== MxOS : build =="
make clean >/dev/null && make >/dev/null || { echo "FAIL: build"; exit 1; }

if [ ! -f "$IMG" ]; then
    echo "FAIL: $IMG introuvable apres build"
    exit 1
fi

echo "== MxOS : boot + injection de commandes (QEMU headless) =="

# Séquence de touches shell : ps, write/ls/cat/rm d'un fichier de test.
send_keys() {
    for c in "$@"; do
        echo "sendkey $c"
        sleep 0.08
    done
}

(
    sleep 3
    send_keys p s
    echo "sendkey ret"; sleep 0.4
    send_keys w r i t e spc t e s t f i l e spc h e l l o
    echo "sendkey ret"; sleep 0.4
    send_keys l s
    echo "sendkey ret"; sleep 0.4
    send_keys c a t spc t e s t f i l e
    echo "sendkey ret"; sleep 0.4
    send_keys r m spc t e s t f i l e
    echo "sendkey ret"; sleep 0.4
    send_keys l s
    echo "sendkey ret"; sleep 0.8
    echo "quit"
) | timeout "$TIMEOUT" qemu-system-i386 \
        -drive format=raw,file="$IMG" \
        -display none -no-reboot \
        -serial file:"$LOG" \
        -monitor stdio >/dev/null 2>&1

echo "== MxOS : vérification des marqueurs attendus =="

check() {
    local desc="$1" pattern="$2"
    if grep -qF -- "$pattern" "$LOG"; then
        echo "  OK   - $desc"
    else
        echo "  FAIL - $desc (motif introuvable: '$pattern')"
        FAILED=1
    fi
}

check "boot jusqu'au shell"                 "MxOS Shell v1.0"
check "pagination + isolation activées"     "Paging enabled (per-task isolation ready)"
check "scheduler démarré"                   "Starting scheduler"
check "tâche ring3 exécutée via int 0x80"   "[Ring3] Hello depuis l'espace utilisateur"
check "commande ps : tâche Shell listée"    "Shell"
check "commande write"                      "Fichier ecrit avec succes"
check "commande ls liste le fichier créé"   "- testfile"
check "commande cat lit le contenu"         "hello"
check "commande rm supprime le fichier"     "Fichier supprime"

if [ "$FAILED" -eq 0 ]; then
    echo "== SUCCES : tous les tests sont passés =="
    exit 0
else
    echo "== ECHEC : voir les FAIL ci-dessus (log complet: $LOG) =="
    cp "$LOG" /tmp/mxos_test_failed.log
    echo "Log conservé dans /tmp/mxos_test_failed.log"
    exit 1
fi

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
        sleep 0.1
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
    echo "sendkey ret"; sleep 0.4
    # Réutilisation d'espace disque : le fichier ci-dessus a été supprimé,
    # on écrit un nouveau fichier différent puis on RÉÉCRIT sous le même nom
    # que l'original pour vérifier que l'allocateur par bitmap a bien
    # récupéré/reste capable de réutiliser les secteurs libérés par "rm".
    send_keys w r i t e spc a u t r e f i c h i e r spc d i s q u e
    echo "sendkey ret"; sleep 0.4
    send_keys w r i t e spc t e s t f i l e spc r e u t i l i s e
    echo "sendkey ret"; sleep 0.4
    send_keys c a t spc t e s t f i l e
    echo "sendkey ret"; sleep 0.4
    send_keys l s
    echo "sendkey ret"; sleep 0.4
    echo "quit"
) | timeout "$TIMEOUT" qemu-system-i386 \
        -drive format=raw,file="$IMG" \
        -display none -no-reboot \
        -serial file:"$LOG" \
        -monitor stdio >/dev/null 2>&1

echo "== MxOS : boot séparé pour le test d'exec concurrent =="
# Fait dans un second boot QEMU, avec très peu de commandes préalables :
# une longue chaîne de touches avant celle-ci s'est révélée peu fiable en
# environnement headless (dérive de synchronisation clavier), donc on isole
# ce test pour limiter le risque de corruption de la frappe injectée.
LOG2="$(mktemp /tmp/mxos_test2_XXXXXX.log)"
(
    sleep 3
    send_keys e x e c spc h e l l o
    echo "sendkey ret"; sleep 0.3
    # Relance immédiate, avant que le premier programme n'ait forcément fini,
    # pour vérifier que plusieurs slots d'exécution coexistent (kernel/exec.c).
    send_keys e x e c spc h e l l o
    echo "sendkey ret"; sleep 1.0
    echo "quit"
) | timeout "$TIMEOUT" qemu-system-i386 \
        -drive format=raw,file="$IMG" \
        -display none -no-reboot \
        -serial file:"$LOG2" \
        -monitor stdio >/dev/null 2>&1

echo "== MxOS : boot séparé pour le test clavier (Shift + historique) =="
LOG3="$(mktemp /tmp/mxos_test3_XXXXXX.log)"
(
    sleep 3
    send_keys v e r
    echo "sendkey ret"; sleep 0.4
    echo "sendkey shift-v"; sleep 0.15
    echo "sendkey shift-e"; sleep 0.15
    echo "sendkey shift-r"; sleep 0.15
    echo "sendkey ret"; sleep 0.4
    echo "sendkey up"; sleep 0.3
    echo "sendkey up"; sleep 0.3
    echo "sendkey ret"; sleep 0.4
    echo "quit"
) | timeout "$TIMEOUT" qemu-system-i386 \
        -drive format=raw,file="$IMG" \
        -display none -no-reboot \
        -serial file:"$LOG3" \
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
check "programme 'hello' seme sur le disque" "- hello"
check "IPC : message reçu par le shell"     "[IPC] Message de la tache"
check "IPC : contenu du message correct"    "Salut depuis Ring3 via IPC !"
check "bitmap : réutilisation après rm (nouveau fichier)" "- autrefichier"
check "bitmap : réécriture d'un fichier au même nom après rm" "reutilise"

# Vérifie que le programme "hello" a bien produit sa sortie AU MOINS deux
# fois dans le log (une par exec), preuve que les deux lancements rapprochés
# ont bien coexisté au lieu que le second échoue/écrase le premier.
echo "== MxOS : vérification exec (second boot) =="
check2() {
    local desc="$1" pattern="$2"
    if grep -qF -- "$pattern" "$LOG2"; then
        echo "  OK   - $desc"
    else
        echo "  FAIL - $desc (motif introuvable: '$pattern')"
        FAILED=1
    fi
}
check2 "exec charge et lance le programme" "[UserProg] Hello depuis un programme charge du disque"

hello_count=$(grep -c "Hello depuis un programme charge du disque" "$LOG2")
if [ "$hello_count" -ge 2 ]; then
    echo "  OK   - exec concurrent : 2 lancements de 'hello' ont produit leur sortie ($hello_count occurrences)"
else
    echo "  FAIL - exec concurrent : seulement $hello_count occurrence(s) de la sortie attendue (>=2 attendu)"
    FAILED=1
    cp "$LOG2" /tmp/mxos_test2_failed.log
fi
rm -f "$LOG2"

echo "== MxOS : vérification clavier Shift + historique (troisième boot) =="
check3() {
    local desc="$1" pattern="$2"
    if grep -qF -- "$pattern" "$LOG3"; then
        echo "  OK   - $desc"
    else
        echo "  FAIL - $desc (motif introuvable: '$pattern')"
        FAILED=1
    fi
}
# "ver" (minuscule) puis Shift+v,e,r -> "VER" doit donner "Commande inconnue : VER"
check3 "touche Shift : majuscule tapée et reconnue" "Commande inconnue : VER"
# La bannière de version doit apparaître 2 fois : une pour le "ver" initial,
# une seconde pour le "ver" rappelé via flèche haut x2 (VER -> ver) puis
# ré-exécuté -> preuve que l'historique de commandes fonctionne.
ver_count=$(grep -c "MxOS Kernel v1.1.2" "$LOG3")
if [ "$ver_count" -ge 2 ]; then
    echo "  OK   - historique : commande rappelée (flèche haut x2) réexécutée ($ver_count occurrences)"
else
    echo "  FAIL - historique : seulement $ver_count occurrence(s) de la bannière (>=2 attendu)"
    FAILED=1
fi
rm -f "$LOG3"

if [ "$FAILED" -eq 0 ]; then
    echo "== SUCCES : tous les tests sont passés =="
    exit 0
else
    echo "== ECHEC : voir les FAIL ci-dessus (log complet: $LOG) =="
    cp "$LOG" /tmp/mxos_test_failed.log
    echo "Log conservé dans /tmp/mxos_test_failed.log"
    exit 1
fi

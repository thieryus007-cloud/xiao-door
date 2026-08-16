# Étape 0 — Préparation de l'environnement

Objectif : avoir une chaîne de compilation/flash fonctionnelle pour le XIAO nRF54LM20A Sense avant de toucher au code Matter.

## Checklist

- [ ] Installer nRF Connect SDK (dernière version stable supportant le nRF54LM20A) via nRF Connect for Desktop → Toolchain Manager, ou `west` en ligne de commande
- [ ] Installer VS Code + extension **nRF Connect for VS Code**
- [ ] Cloner / récupérer le board support Seeed pour le XIAO nRF54LM20A Sense
  - Réf : https://wiki.seeedstudio.com/xiao_nrf54lm20a_getting_started/
- [ ] Compiler un exemple simple (Blinky ou Hello World) pour la board XIAO nRF54LM20A Sense
- [ ] **Connecter le XIAO en USB-C** et flasher l'exemple compilé
- [ ] Vérifier que la LED clignote / que la sortie série (Hello World) s'affiche correctement

## 🔌 Quand connecter le XIAO

Tu n'as **pas besoin de connecter le board tout de suite**. La première connexion physique interviendra à la dernière étape ci-dessus, une fois que :
1. Le SDK est installé et fonctionnel
2. Un exemple Blinky/Hello World est compilé sans erreur

Je te préviendrai explicitement ici avec le repère **🔌 CONNECTER LE XIAO MAINTENANT** quand on y sera.

## État d'avancement

| Sous-étape | Statut |
|---|---|
| Installation nRF Connect SDK | ⬜ à faire |
| Extension VS Code | ⬜ à faire |
| Board support Seeed | ⬜ à faire |
| Compilation Blinky | ⬜ à faire |
| Flash + validation matérielle | ⬜ à faire (nécessite le XIAO connecté) |

## Notes techniques

_(À compléter au fur et à mesure — versions installées, chemins SDK, problèmes rencontrés et solutions)_

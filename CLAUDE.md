# Istruzioni per Claude in questo fork personale

Questo repository è un fork personale di FreeCAD. Valgono le regole di [AGENTS.md](AGENTS.md),
compresa l'eccezione per i messaggi di commit del fork.

Remote:

- `origin`: repository ufficiale FreeCAD (https://github.com/FreeCAD/FreeCAD), solo per leggere.
  Non pushare mai su `origin`.
- `fork`: repository personale (https://github.com/FloppyO1/enhanced-FreeCAD), dove si pubblicano
  i branch delle funzionalità.

## Metodo di lavoro: orchestrazione con agenti

Adotta sempre la skill [orchestrazione-agenti](.claude/skills/orchestrazione-agenti/SKILL.md):
lavora come project manager che delega ai subagenti (haiku/sonnet, al massimo un opus alla
volta), verifica tu i risultati e tiene aggiornato `STATO.md`. Nei brief per questo repository
ricorda: build con `build-freecad.bat noupdate`, un solo job di compilazione alla volta
(la build usa molta RAM), nessun processo lasciato appeso, e nessun commit da parte degli agenti.

## Branch principale: `personal`

`personal` è il branch principale del fork: contiene tutte le modifiche personali ed è quello che
si compila per l'uso quotidiano. `main` serve solo a seguire il repository ufficiale. I nuovi
branch delle funzionalità partono **sempre** da `personal`, mai da `main`. Anche le modifiche
ufficiali, una volta portate in `main`, vanno poi unite in `personal` con `git merge main`.

## Prima di ogni nuova funzionalità: chiedere se allineare al main ufficiale

Prima di iniziare a lavorare su una richiesta di nuova funzionalità, chiedi sempre se si vuole
allineare il progetto al `main` del repository ufficiale, così da portare dentro le ultime
modifiche di FreeCAD. Prima di chiedere, mostra quanti commit nuovi ci sono
(`git fetch origin`, poi `git rev-list --count main..origin/main`).

Se la risposta è sì:

1. Controlla che il working tree sia pulito; se non lo è, chiedi cosa fare delle modifiche.
2. Aggiorna `main` locale solo in fast-forward: `git switch main`, poi
   `git merge --ff-only origin/main`. Se il fast-forward non è possibile, fermati e spiega perché.
3. Pubblica il `main` aggiornato sul fork: `git push fork main`.
4. Unisci il `main` aggiornato in `personal` (`git switch personal`, poi `git merge main`) e crea
   il branch della nuova funzionalità a partire da `personal`.
5. Per i branch di funzionalità già esistenti chiedi se vanno aggiornati anche loro. Preferisci
   `git merge main` al rebase, perché i branch sono già pubblicati e il rebase richiederebbe un
   force push. Risolvi i conflitti solo spiegandoli prima.
6. Ricorda che dopo l'allineamento serve ricompilare FreeCAD (la build è più lunga del solito).

Se la risposta è no, crea il branch della nuova funzionalità da `personal` senza aggiornare `main`.

# obspm — OBS Project Manager

Plugin per OBS Studio che organizza le registrazioni in **progetti** e passa in automatico tra setup **orizzontale** e **verticale**.

- Una cartella di lavoro principale; ogni progetto è una sottocartella (elenco in `obspm.json`).
- Al primo "Avvia registrazione" di ogni sessione scegli progetto, orientamento e risoluzione.
- obspm carica il profilo e la raccolta scene giusti (`obspm-h` / `obspm-v`), imposta la risoluzione e salva le registrazioni nella cartella del progetto.
- Progetti creabili, rinominabili ed eliminabili direttamente dal plugin.

Requisiti: **OBS Studio 31 o successivo** (macOS 12+, Windows 10/11 x64, Ubuntu 24.04 x86_64).

---

## Installazione

Scarica il pacchetto per il tuo sistema dalla pagina **Releases** del repository
(oppure, per una build non rilasciata, da **Actions → ultima esecuzione → Artifacts**).

> Chiudi OBS prima di installare o aggiornare il plugin.

### macOS

**Con l'installer (`.pkg`, dalle Releases)**
1. Apri `obspm-<versione>-macos-universal.pkg` e segui la procedura.
2. Se macOS blocca il file perché non firmato: *Impostazioni di Sistema → Privacy e sicurezza → Apri comunque*.

**Manuale (`.tar.xz`)**
1. Estrai l'archivio: contiene `obspm.plugin`.
2. Copialo nella cartella plugin di OBS:
   ```sh
   mkdir -p ~/Library/Application\ Support/obs-studio/plugins
   cp -R obspm.plugin ~/Library/Application\ Support/obs-studio/plugins/
   ```
3. Se il plugin non si carica perché non firmato, rimuovi la quarantena:
   ```sh
   xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/obspm.plugin
   ```

### Windows

**Con l'installer (`.exe`, dalle Releases)** — eseguilo e segui la procedura.

**Manuale (`.zip`)**
1. Estrai l'archivio: contiene la cartella `obspm` (con dentro `bin\64bit\obspm.dll`).
2. Copia la cartella `obspm` in `C:\ProgramData\obs-studio\plugins\`
   (crea la cartella `plugins` se non esiste). Risultato:
   `C:\ProgramData\obs-studio\plugins\obspm\bin\64bit\obspm.dll`

### Linux (Ubuntu 24.04)

**Pacchetto `.deb` (dalle Releases)**
```sh
sudo apt install ./obspm-<versione>-x86_64-ubuntu-gnu.deb
```

**Manuale (`.tar.xz`)** — copia `obspm.so` nella cartella plugin dell'utente:
```sh
mkdir -p ~/.config/obs-studio/plugins/obspm/bin/64bit
cp obspm.so ~/.config/obs-studio/plugins/obspm/bin/64bit/
```

### Verifica

Apri OBS: deve comparire la finestra **"Welcome to obspm"**. In alternativa controlla
*Aiuto → File di log → Mostra file di log attuale* e cerca `[obspm] plugin loaded`.

---

## Primo avvio

1. **Welcome to obspm**: scegli la cartella di lavoro principale (proposta: `Filmati/obspm`) e premi **Continue**.
   Con **Later** te lo richiederà al primo "Avvia registrazione".
2. obspm crea i profili e le raccolte scene **`obspm-h`** e **`obspm-v`**
   (i profili copiano encoder e audio dal tuo profilo attuale).
3. **Prepara le scene una volta sola**: *Raccolta scene → obspm-h* per l'orizzontale e *obspm-v* per il verticale.

## Uso

- **Avvia registrazione** (prima volta nella sessione) apre la finestra di sessione:
  1. **Progetto** — scegline uno o seleziona *"+ New project…"* e scrivi il nome.
     Con **Rename…** / **Delete…** rinomini o elimini un progetto (l'eliminazione può
     togliere solo la voce dalla lista o spostare la cartella nel Cestino).
  2. **Orientamento** — Horizontal o Vertical.
  3. **Risoluzione** — un preset per l'orientamento scelto.

  **● Start recording** applica tutto e avvia la registrazione. Le registrazioni successive della sessione vanno direttamente nella stessa cartella.
- **Pulsante `obspm · …` nel pannello Controlli** — mostra progetto/orientamento attivi e riapre la stessa finestra per cambiarli (**✓ Apply**, senza avviare la registrazione). Disattivato durante la registrazione.
- **Strumenti → obspm Settings** — cartella di lavoro e risoluzioni, anche personalizzate.

Preset disponibili:

| Orizzontale | Verticale |
|---|---|
| 1280×720 | 720×1280 |
| 1920×1080 | 1080×1350 (4:5) |
| 2560×1440 | 1080×1920 |
| 3840×2160 | 1440×2560 |
| | 2160×3840 |

> Il dialogo si apre solo cliccando il pulsante di registrazione: se avvii con un tasto rapido, registra nel progetto già attivo senza chiedere.

## Dove salva i dati

| Cosa | Dove |
|---|---|
| Configurazione plugin | `<config OBS>/plugin_config/obspm/config.json` |
| Elenco progetti | `<cartella di lavoro>/obspm.json` |
| Registrazioni | `<cartella di lavoro>/<progetto>/` |

`<config OBS>` è `~/Library/Application Support/obs-studio` (macOS), `%APPDATA%\obs-studio` (Windows), `~/.config/obs-studio` (Linux).

## Disinstallazione

Elimina il plugin dalla cartella in cui l'hai installato (vedi sopra); su Linux con `.deb`: `sudo apt remove obspm`.
Profili e raccolte scene `obspm-*` restano in OBS: se non servono, eliminali dai menu *Profilo* e *Raccolta scene*.

---

## Sviluppo

### Build locale (macOS)

Requisiti: Xcode 16+, CMake 3.28+ (`brew install cmake`).

```sh
cmake --preset macos            # la prima volta scarica sorgenti OBS, Qt e dipendenze in .deps/
scripts/install.sh              # compila e installa in ~/Library/Application Support/obs-studio/plugins
scripts/reset.sh                # riporta il plugin allo stato "appena installato" (OBS chiuso)
```

`reset.sh` elimina la configurazione del plugin, i profili e le raccolte `obspm-h`/`obspm-v`;
**non** tocca registrazioni né `obspm.json`.

Windows e Linux: `cmake --preset windows-x64` / `cmake --preset ubuntu-x86_64`, poi `cmake --build --preset <stesso nome>`.

### CI (GitHub Actions)

Le workflow in `.github/workflows` compilano il plugin per **macOS, Windows e Ubuntu**:

| Evento | Cosa succede |
|---|---|
| Push su `main` / PR | Build sui 3 sistemi + controllo formattazione; pacchetti negli **Artifacts** dell'esecuzione |
| *Actions → Dispatch → Run workflow* | Build manuale |
| Tag `X.Y.Z` (es. `git tag 1.0.0 && git push --tags`) | Build + pacchetti installer + **bozza di Release** con tutti i file |

La versione è in `buildspec.json`. Per firmare/notarizzare su macOS configura i secret descritti nella
[wiki del plugin template](https://github.com/obsproject/obs-plugintemplate/wiki); senza, i pacchetti macOS sono non firmati.

Formattazione richiesta dal CI: `clang-format` 19 per il C++ e `gersemi` per i file CMake.

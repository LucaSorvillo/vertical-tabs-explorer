# VTabsExplorer — note di progettazione

> Note di sviluppo dettagliate, in italiano, scritte man mano durante lo sviluppo: come funziona ogni parte e
> perché è fatta così. Alcune sezioni raccontano anche tentativi poi cambiati. Per l'uso c'è il
> [README](README.it.md); un riepilogo tecnico in inglese è in [ARCHITECTURE.md](ARCHITECTURE.md).

Estensione per **Esplora file di Windows 11**, sulla stessa linea di VTabsNotepad:
**ricorda le finestre e le schede aperte e le riapre** quando Esplora file viene riaperto, come fa il
Blocco note con le sue schede (anche dopo un riavvio del PC).

Le funzioni di base non modificano Esplora file e non iniettano nulla: un piccolo programma nella tray
osserva le finestre dall'esterno con le API della shell e le ricrea quando serve. Nella tray ha
un'icona monocroma a contorno: una cartella con la barretta piena in basso al centro, che richiama Esplora
file senza essere la sua icona.
- È bianca sulla barra delle applicazioni scura e nera su quella chiara, come le icone di sistema.
- È disegnata con GDI+ alla misura vera, a ogni cambio di scala e di tema. La griglia di 16 unità è
  agganciata ai pixel, quindi le linee restano nitide anche a 20 px (schermo al 125%).
- Se GDI+ non parte, usa il glifo "History" di Segoe Fluent Icons. L'unica eccezione,
facoltativa e spenta finché non la si attiva dal menu, sono le **schede verticali** (come in
VTabsNotepad): una mod (`VTabsExplorerMod.dll`) che lavora dentro Esplora file, con le protezioni descritte
più avanti.

## Cosa fa

- Mentre si lavora salva di continuo finestre, **schede nell'ordine in cui sono**, scheda attiva e
  **posizione e dimensione** di ogni finestra (ingrandita compresa).
- **Chiudendo l'ultima finestra** ricorda lei e quelle chiuse insieme a lei (a pochi secondi l'una
  dall'altra: "Chiudi tutte le finestre", spegnimento del PC, riavvio di Esplora file). Una finestra
  chiusa mentre altre restano aperte viene dimenticata, come nel Blocco note.
- **Riaprendo Esplora file** (con nessuna finestra aperta):
  - se si apre nella cartella iniziale (Home, Questo PC o quella scelta in "Apri Esplora file in"), la
    finestra diventa la prima della sessione e le altre finestre si riaprono nella loro posizione;
  - se si apre su una cartella precisa (es. "Mostra nella cartella" da un altro programma), quella resta
    la scheda attiva e le schede della sessione si aggiungono accanto.
- **Le finestre compaiono già complete**, come le schede del Blocco note o di un browser: mentre le
  schede si costruiscono la finestra resta invisibile (velata) e compare tutta insieme, con le schede
  nell'ordine giusto, la scheda attiva e la sua posizione. Esplora file impiega circa 0,3 s per ogni
  scheda in più: una finestra con 4-5 schede compare circa 1-1,5 s dopo il solito.
- Cartelle che non esistono più vengono saltate; le cartelle virtuali (Questo PC, Home, Raccolte,
  Pannello di controllo...) tornano come le altre.
- Con le finestre già aperte, aprirne un'altra non riapre nulla.
- **Nessuna scheda persa**: se Esplora file è troppo occupato (per esempio appena riavviato) e una
  scheda non compare, la tray la chiede di nuovo; se non arriva nemmeno così, la scheda (o la finestra)
  resta da riaprire con Ctrl+Maiusc+T e un fumetto della tray lo dice.

- **Pulsante "Torna su"**: quando la vista dei file della finestra in primo piano è scorsa verso il
  basso, nell'angolo in basso a destra (accanto alla barra di scorrimento) compare sfumando un pulsante
  tondo con la freccia in su, come quello di VTabsNotepad; il clic riporta la vista all'inizio con uno
  scorrimento animato, senza togliere il focus a Esplora file. Sparisce in cima alla cartella, quando
  si passa a un'altra finestra o programma e mentre la finestra si sposta o è ridotta a icona; segue
  la scheda attiva e la cartella mostrata.

- **Riapri la scheda chiusa con Ctrl+Maiusc+T**, come nel browser: la scheda chiusa per sbaglio torna
  al suo posto, attiva e con il suo colore (dalla 1.2.0: Esplora file sa solo aggiungerla in fondo, la tray scrive
  nel file dei colori `move=da,a` e la mod la riporta dov'era con lo stesso spostamento del riordino; senza le
  schede verticali resta in fondo) e premendo di nuovo si va a
  ritroso, fino a 25 schede. Se è stata chiusa un'intera finestra torna la finestra, con tutte le sue
  schede, la scheda attiva e la sua posizione (velata mentre si costruisce: compare già completa). Una
  scheda la cui finestra non c'è più si riapre in quella in uso. Come in Chrome **la pila resta**: si chiude
  una scheda, si chiude la finestra, si riapre Esplora file (torna la sessione) e Ctrl+Maiusc+T riporta la
  scheda; vale anche dopo aver riavviato la tray o il PC. Si ricordano solo i percorsi delle cartelle, come
  nella sessione salvata, in `%LOCALAPPDATA%\VTabsExplorer\closed.txt` (`closed-dev.txt` in modalità prova).
  Riaprendo una
  finestra chiusa, da quel momento la sessione salvata sono le finestre aperte, come se l'avessi aperta
  tu: per riavere tutte le finestre di un colpo c'è "Riapri ora l'ultima sessione".
  La combinazione viene chiesta a Windows **solo mentre è attiva una finestra di Esplora file** e
  restituita appena si passa ad altro, così resta disponibile dove la usano gli altri programmi (nei
  browser, per esempio); della tastiera non si legge nient'altro.

- **Cartelle aperte da altri programmi in una scheda** (voce "Cartelle aperte da altri programmi in una
  scheda (come Chrome)" nel menu della tray, attiva in partenza), come Chrome con i link e il Blocco note
  con i file: quando un programma apre una cartella (OBS "Mostra le registrazioni", IntelliJ "Mostra in
  Esplora file", un collegamento sul desktop, la ricerca di Start...) mentre una finestra di Esplora file è
  già aperta, la cartella arriva in una **scheda nuova** di quella finestra, in fondo alla barra e attiva, e
  la finestra passa in primo piano; se il programma voleva mostrare un file, il file è selezionato anche
  nella scheda. Con più finestre la scheda va in quella **usata o aperta per ultima** (nel desktop virtuale
  attuale). Restano finestre a sé: Ctrl+N e "Apri in una nuova finestra" (la richiesta viene da Esplora file
  stesso), Win+E e "Esplora file" da barra delle applicazioni o Start (aprono la cartella iniziale), la prima
  finestra e quelle riaperte da VTabsExplorer. La finestra che Windows apre non si vede: nasce velata e si chiude
  appena la scheda è pronta (circa 1 s dal clic alla scheda).

- **Schede verticali** (facoltative, voce "Schede verticali (colonna a sinistra)" nel menu della
  tray), come in VTabsNotepad: le schede diventano una colonna a sinistra che va dalla **cima della finestra**
  al fondo, come le schede verticali di Chrome; l'intestazione e la barra delle cartelle cominciano dal suo
  bordo destro, sopra il contenuto della cartella. La riga del titolo, rimasta vuota con le schede nella
  colonna, **si nasconde** (come Edge con "nascondi la barra del titolo"): la barra degli indirizzi è la prima
  riga, accanto ai pulsanti riduci/ingrandisci/chiudi e fino a loro, e barra dei comandi, barra delle
  cartelle e contenuto salgono della stessa altezza (48 px al 125%). I pulsanti riduci/ingrandisci/chiudi
  sono **alti quanto la barra degli indirizzi**, come in Chrome e nel Terminale di Windows (quelli di sistema
  restavano più bassi della riga): stesso aspetto di quelli di Windows 11 (chiudi rosso al passaggio,
  suggerimenti, menu di sistema col tasto destro, layout di ancoraggio su ingrandisci se attivi nelle
  impostazioni di Windows). In cima alla colonna, allineati alla
  barra degli indirizzi, c'è comprimi/espandi; lo spazio vuoto lì intorno e il tratto libero
  a sinistra dei pulsanti della finestra sono barra del titolo (si trascina, doppio clic ingrandisce, clic
  destro apre il menu di sistema); il bordo in cima ridimensiona come prima. Con la finestra stretta
  l'intestazione ha meno spazio: la barra dei comandi sposta prima le sue voci nel menu "Altre opzioni" (…)
  e la casella di ricerca si accorcia. Ogni scheda è una riga con l'icona e il
  titolo della scheda originale (stesso carattere, dimensione, peso e colore, letti dalle schede native),
  su una "card" appena velata; quella attiva ha il fondo più deciso, un bordino e la stanghetta a
  sinistra. La "X" compare sulla scheda attiva e al passaggio del mouse, il clic centrale chiude, il "+"
  sta subito sotto l'ultima scheda. In alto: **comprimi/espandi** (da compressa la colonna mostra solo
  le icone); la ricerca tra le schede (lente e casella "Cerca nelle schede") è stata tolta il 2026-11-05,
  su richiesta dell'utente. Si **riordinano trascinando** le righe (l'ordine cambia anche in Esplora file,
  la scheda attiva resta quella); il **clic destro** apre il menu originale della scheda (Chiudi scheda,
  Chiudi altre schede, Chiudi schede a destra, Duplica scheda); la **larghezza** si cambia trascinando
  la linea divisoria (doppio clic: torna a 240) e vale per tutte le finestre, come lo stato compresso.
  Dopo un clic su una riga la tastiera torna alla vista dei file. La barra del titolo, rimasta vuota, si
  trascina anche dove c'erano le schede. Spegnendo la voce torna esattamente la barra orizzontale.
  **Colore scheda**, come in VTabsNotepad: clic destro su una riga → "Colore scheda" (in fondo al menu
  originale) → Nessuno, Grigio, Blu, Rosso, Giallo, Verde, Rosa, Viola, Azzurro, Arancione. La card si
  tinge appena (di più sulla scheda attiva) e la stanghetta a sinistra prende il colore; il nome del
  colore è esposto anche a UI Automation (stato della riga). Il colore appartiene alla **scheda**, non alla
  cartella: la segue nel riordino e se cambia cartella, si salva con la sessione e torna riaprendo
  Esplora file, con "Riapri ora" e con Ctrl+Maiusc+T. Si vede solo con le schede verticali accese.
  **Intestazione del colore della barra del titolo** (sempre, con le schede verticali; nessuna voce di
  menu): la barra degli indirizzi (indietro, avanti, su, aggiorna, percorso, ricerca) e la
  barra dei comandi (Nuovo, taglia, copia, incolla, Ordina, Visualizza...) prendono lo sfondo della barra
  del titolo, lo stesso della colonna delle schede: barra del titolo, barre e colonna diventano una
  cornice unica attorno al contenuto della cartella. Caselle, pulsanti e le due linee sottili fra le barre
  restano come sono; segue il tema chiaro/scuro.

- **Riquadro di spostamento in alto** (voce "Riquadro di spostamento in alto (barra delle cartelle)" nel
  menu della tray, con o senza schede verticali): il riquadro di spostamento diventa una barra
  orizzontale sotto la barra dei comandi, come la barra dei segnalibri di Chrome.
  - Contenuto: le stesse voci di primo livello del riquadro (Home, Galleria, OneDrive, le cartelle
    aggiunte ad Accesso rapido, Questo PC, Rete, Linux...), nello stesso ordine, con le loro icone.
    Ogni voce è riportata così com'è nel riquadro, senza reinterpretazioni: anche le cartelle aggiunte
    con un nome invisibile sono voci normali, con la loro icona, e si aprono con un clic.
  - Divisori: gli stessi del riquadro di sistema. C'è una linea prima della prima cartella aggiunta e una
    prima della prima voce dopo le cartelle aggiunte (Questo PC). Nella barra è verticale, come i
    separatori della barra dei comandi; nell'elenco "»" è orizzontale.
  - "»": le voci che non ci stanno finiscono in fondo, in questo elenco; con molte voci l'elenco scorre
    invece di salire sopra la barra.
  - Clic: apre la cartella nella scheda attiva.
  - Clic centrale o Ctrl+clic: la apre in una scheda nuova.
  - Clic destro: il **menu nuovo di Windows 11**, lo stesso del riquadro di sistema: icone in alto (Copia),
    Apri in una nuova scheda, Apri in una nuova finestra, Rimuovi da Accesso rapido, Aggiungi a Start,
    Comprimi in..., Copia come percorso, Nuovo, Proprietà, le estensioni installate e "Mostra altre
    opzioni", che apre il menu classico completo. Con Maiusc+clic destro si apre subito il menu classico,
    come nel riquadro. Mancano le voci che servono solo nell'albero: "Rinomina" ed "Espandi"/"Comprimi".
    Clic, clic centrale e clic destro valgono anche nell'elenco "»".
  - **Spostare le cartelle aggiunte**: si trascinano, come nel riquadro di sistema. Una linea col colore
    d'accento segna dove andranno. Si può trascinare:
    - nella barra;
    - nell'elenco "»";
    - dall'elenco alla barra;
    - dalla barra all'elenco: fermandosi un attimo su "»" l'elenco si apre.

    Il nuovo ordine è quello di Windows: lo stesso del riquadro, della Home e del menu dell'icona di
    Esplora file sulla barra delle applicazioni. Si trascinano solo le cartelle aggiunte (e i gruppi), non
    Home, Galleria, Questo PC, Rete... (come nel riquadro), e restano nel blocco delle cartelle aggiunte.
    Esc annulla, e un trascinamento non apre la cartella.
  - **Gruppi**, come le cartelle dei segnalibri di Chrome: raccolgono cartelle aggiunte e altri gruppi,
    anche uno dentro l'altro, e nella barra hanno l'icona di una pila di cartelle (solo contorno, bianca nel
    tema scuro e nera in quello chiaro, come le icone della barra dei comandi). Il gruppo vive solo nella
    barra: le cartelle restano aggiunte ad Accesso rapido, e la Home e il riquadro di sistema non cambiano.
    - **Nuovo gruppo**: clic destro sullo sfondo della barra (in fondo alle cartelle aggiunte), nel menu
      di un gruppo (un sottogruppo) o nel menu classico di una cartella aggiunta, con "Sposta nel gruppo" >
      "Nuovo gruppo…" (il gruppo prende il posto della cartella e la contiene). Nasce come "Nuovo
      gruppo" e si apre subito il riquadro del nome: Invio o "Salva" lo cambiano, Esc o "Annulla" lo
      lasciano così, come per una cartella nuova.
    - Clic: l'elenco del contenuto, sotto il gruppo. I sottogruppi si aprono a lato, con un clic, con la
      freccia destra o fermandosi un attimo sopra. Un gruppo vuoto dice "(vuoto)". Con un elenco aperto,
      basta passare sopra un altro gruppo della barra (o su "»") e il suo elenco si apre subito, senza
      animazioni, al posto di quello aperto, come in Chrome. Il pulsante del gruppo aperto resta
      evidenziato nella barra (lo sfondo del passaggio del mouse) finché il suo elenco è aperto.
    - Clic centrale: tutte le sue cartelle in schede nuove (sopra le 15 chiede conferma, come Chrome).
    - Clic destro: "Apri tutte (N)", "Nuovo gruppo…", "Rinomina…", "Elimina gruppo". Eliminando un
      gruppo il contenuto passa al livello di sopra, al suo posto: nessuna cartella esce da Accesso rapido.
    - Nel menu classico di una cartella aggiunta ("Mostra altre opzioni" nel menu nuovo, o Maiusc+clic
      destro): "Sposta nel gruppo", con tutti i gruppi (quelli annidati col percorso, "A › B"; spuntato
      quello attuale) e, se la cartella è in un gruppo, "Togli dal gruppo", che la rimette subito dopo il
      gruppo. Nel menu nuovo non possono stare: Windows ci mette solo i suoi verbi e le estensioni
      installate.
    - Trascinando: una cartella o un gruppo lasciati al centro di un gruppo ci entrano (il gruppo si
      illumina), vicino ai bordi vanno prima o dopo. Fermandosi sopra un gruppo il suo elenco si apre, e lì
      si lasciano nel punto preciso. Si trascina anche dagli elenchi alla barra e da un gruppo all'altro:
      trascinando dall'elenco di un gruppo (o di un sottogruppo) e fermandosi su un altro gruppo della barra
      (o su "»", o su un sottogruppo accanto), l'elenco di partenza si chiude e si apre quello del gruppo,
      come in Chrome. Un gruppo non entra in sé stesso né in un suo sottogruppo.
    - **Nel riquadro di spostamento di sistema** (e nella Home, nel menu dell'icona sulla barra delle
      applicazioni) le cartelle stanno nell'ordine della barra letta in profondità: quelle di primo livello al
      loro posto e, al posto di ogni gruppo, le sue cartelle nel suo ordine, con quelle dei sottogruppi prima di
      passare alla voce successiva. La barra lo sistema a ogni operazione sui gruppi e, con la barra accesa o
      spenta, una volta all'avvio di Esplora file (i gruppi fatti prima si mettono in ordine da soli). Il
      riquadro delle schede già aperte, che da sé non rilegge mai l'ordine, viene rimesso in pari: spegnendo la
      barra lo si trova già in ordine.
    - Una cartella tolta da Accesso rapido dal menu della barra esce anche dal suo gruppo (riaggiunta,
      torna al primo livello).
    - Si salvano in `%LOCALAPPDATA%\VTabsExplorer\navbar-groups.json` (le finestre di prova usano un file
      a parte).
  - Il riquadro verticale sparisce solo dove c'è la barra: l'impostazione di Esplora file non cambia, e
    spegnendo la voce torna com'era.
  - **Segue "Riquadro di spostamento"** (Visualizza > Mostra): spento, la barra sparisce e il contenuto
    torna subito sotto la barra dei comandi, come farebbe il riquadro verticale; riacceso, la barra torna.
    Come il riquadro, vale scheda per scheda: Esplora file lo spegne solo nella scheda attiva, e la barra
    segue la scheda in primo piano (passando a una scheda col riquadro acceso ricompare). Le schede nuove
    si aprono come l'ultima impostazione scelta, e la barra non compare nemmeno un attimo.
  - Aggiornamenti: aggiungendo o togliendo una cartella da Accesso rapido la barra si aggiorna da sé.

Menu della tray: stato della sessione salvata, "Riapri le cartelle all'apertura di Esplora file"
(on/off), "Riapri ora l'ultima sessione" (quando non ci sono finestre aperte), pulsante "Torna su"
nella vista dei file (on/off), "Riapri la scheda chiusa con Ctrl+Maiusc+T" (on/off), "Schede verticali
(colonna a sinistra)" (on/off, spenta all'inizio; se sono state sospese per sicurezza, vedi sotto, una
riga lo dice e il clic le riattiva), "Riquadro di spostamento in alto (barra delle cartelle)" (on/off),
"Avvia con Windows" (attivato al primo avvio), cartella impostazioni e log, "Esci".

Impostazioni e dati: `%LOCALAPPDATA%\VTabsExplorer\` (`settings.ini`, `session.txt`, `logs\`, `colors\`
per i colori delle schede scambiati fra mod e tray, `navbar-groups.json` per i gruppi della barra). Nel
log compaiono solo i conteggi (finestre, schede), mai i percorsi delle cartelle o i titoli delle schede.

## Come funziona

- **Lettura** (`Explorer.cpp`): `IShellWindows` elenca un elemento per ogni scheda, con l'HWND della sua
  finestra; da ognuno si arriva alla cartella (`IShellBrowser` → `IShellView` → `IFolderView` → PIDL),
  salvata come nome di analisi (`C:\...`, `::{GUID}` per le cartelle virtuali) più il PIDL di riserva.
  L'ordine delle schede si legge dalla barra delle schede (XAML) con UI Automation, usando la posizione
  esatta di ogni scheda nell'elenco (`PositionInSet`); la scheda attiva è la prima `ShellTabWindowClass`
  nell'ordine Z dei figli della finestra.
- **Ripristino**: una finestra nuova si apre con il verbo `opennewwindow` (sempre una finestra a sé,
  anche se ce ne sono altre aperte); una scheda nuova con il comando "nuova scheda" della finestra
  (`WM_COMMAND` 0xA21B, come Ctrl+T, lo stesso usato da ExplorerTabUtility) e poi
  `IShellBrowser::BrowseObject`. Le schede si chiedono una dopo l'altra: la successiva appena la
  precedente è attiva, ha la sua vista e risulta tra quelle della shell, senza aspettare che carichi la
  cartella. La scheda attiva si sceglie con i comandi di Ctrl+Tab / Ctrl+Maiusc+Tab (0xA033 / 0xA031,
  dalla tabella degli acceleratori di `ExplorerFrame.dll`), la posizione con `SetWindowPlacement`.
  Esplora file ha un thread per finestra: le altre finestre si riaprono in parallelo, ognuna su un
  thread nostro. Il comando "nuova scheda" parte quando il thread della finestra risponde
  (`SendMessageTimeout` con `WM_NULL`); se dopo 5 s non c'è nessuna scheda nuova si aspetta che la
  finestra torni a rispondere (i messaggi in coda passano) e, se ancora niente, lo si manda una seconda
  volta, mai con la finestra ancora occupata (le schede sarebbero due). Le schede che non si riaprono
  nemmeno così (e le finestre che non si aprono) vanno nella pila di Ctrl+Maiusc+T, tutte insieme per
  finestra, e la tray avvisa con un fumetto (non in modalità prova).
- **Velo** (`Veil.cpp`): un gancio sugli eventi di creazione e comparsa delle finestre del processo di
  Esplora file (`SetWinEventHook`, solo `EVENT_OBJECT_CREATE`..`EVENT_OBJECT_SHOW`: pochi eventi, nessun
  costo misurabile) rende trasparente ogni finestra nuova quando il ripristino potrebbe scattare
  (nessuna finestra aperta, una sessione da riaprire, opzione attiva): stile `WS_EX_LAYERED` con
  opacità zero, impostato dall'esterno nel momento in cui la finestra viene creata, quindi prima che
  sia mai visibile; il mouse la attraversa e resta nella barra delle applicazioni. Esplora file può
  tenere pronta in anticipo una finestra nascosta e mostrare quella: allora la si vela quando compare
  (visibile per qualche millisecondo). A ripristino finito (anche se qualcosa va storto) il velo si
  toglie a tutte le finestre insieme; in ogni caso dopo 10 s, all'uscita della tray e in caso di errore
  fatale. Se non scatta nessun ripristino la finestra torna visibile subito.
- **Pulsante "Torna su"** (`BackToTop.cpp`): Esplora file non si tocca, il pulsante è una nostra piccola
  finestra layered con trasparenza per pixel (disegnata con GDI+: cerchio, bordo e glifo di Segoe Fluent
  Icons con l'antialiasing), sempre sopra, `WS_EX_NOACTIVATE` (il clic non toglie il focus) e fuori da
  barra delle applicazioni e Alt+Tab. Compare solo sopra la finestra di Esplora file in primo piano:
  essendo sempre sopra, su una finestra coperta galleggerebbe sopra le altre. Segnali senza controlli continui:
  la vista dei file (`DirectUIHWND`) emette `EVENT_OBJECT_CONTENTSCROLLED` a ogni scorrimento (a riposo
  nessun evento; UI Automation invece non notifica i cambi di posizione), poi la posizione si legge con
  `ScrollPattern` della lista `UIItemsView`, che è l'elemento radice della vista; `EVENT_SYSTEM_FOREGROUND`,
  `MOVESIZESTART/END`, `MINIMIZESTART/END` per primo piano, spostamenti e riduzione a icona; con una finestra
  di Esplora file in primo piano un controllo locale ogni 200 ms per i cambi di scheda e di cartella (e
  per 3 s dopo il ripristino da icona di una finestra di Esplora file, vedi le note tecniche). Il clic
  riporta in cima con `ScrollPattern.SetScrollPercent` a piccoli passi. Gira su un thread suo.
- **Scorciatoia Ctrl+Maiusc+T** (`Hotkey.cpp`): la combinazione si registra con `RegisterHotKey` solo
  mentre è in primo piano (e non ridotta a icona) una finestra `CabinetWClass` considerata, e si
  restituisce appena il primo piano passa ad altro: Windows ci consegna soltanto questa combinazione,
  nessun altro tasto, e gli altri programmi la riprendono quando tocca a loro. Il primo piano si segue
  con `EVENT_SYSTEM_FOREGROUND` e con gli eventi di riduzione a icona, più un controllo locale ogni
  secondo per i casi in cui l'evento non arriva (ripristinando una finestra Windows può darle il primo
  piano senza segnalarlo) e per riprovare se la combinazione non è ancora nostra (una finestra appena
  aperta viene letta poco dopo; la combinazione può essere momentaneamente tenuta da un altro
  programma). Quel controllo gira **solo mentre si sta in Esplora file**: con altri programmi in primo
  piano si spegne, così a riposo il costo resta quello di prima della funzione.
  Le schede e le finestre chiuse le riconosce `Worker.cpp`: una scheda è chiusa quando la sua
  finestra `ShellTabWindowClass` non esiste più (controllo locale, come per le finestre), e di una
  finestra chiusa si ricorda la finestra intera, non le sue schede una per una. La riapertura usa lo
  stesso codice del ripristino della sessione (velo compreso).
  La pila sta anche su disco, nel formato della sessione con il tipo di ogni voce (`window whole` / `window
  tabs`, `SaveClosedStack`/`LoadClosedStack` in `Session.cpp`). Si riscrive a fine giro del ciclo di lavoro
  quando è cambiata (chiusure, riaperture, finestre tornate con la sessione che `ForgetRestored` toglie) e
  all'uscita, e si rilegge al cambio di modalità, cioè all'avvio della tray: ogni modalità ha il suo file. La
  finestra di provenienza di una scheda non si salva, perché dopo un riavvio la scheda torna in quella in uso.
  Con l'opzione spenta la pila continua a raccogliere, come prima: le schede che un ripristino non riesce a
  riaprire ci finiscono apposta, e il fumetto dice di accendere l'opzione per recuperarle.
- **Sorveglianza** (`Worker.cpp`): ogni mezzo secondo un controllo locale che costa microsecondi
  (finestre `CabinetWClass` e `ShellTabWindowClass` con i loro titoli, nell'ordine Z); solo se cambia
  qualcosa si rileggono le schede con le chiamate a Esplora file. In più posizioni delle finestre (lettura
  locale) e una rilettura completa ogni 30 secondi. A riposo: circa 30 ms di CPU ogni 30 secondi.
- **Sicurezza**: una finestra è "chiusa" solo quando non esiste più (se per un momento non se ne leggono
  le schede resta com'era), e il ripristino scatta solo per una finestra comparsa da pochi secondi, mai
  per una che c'era già. Limiti di 16 finestre e 40 schede per finestra. Se una finestra perde schede
  mentre il suo thread non risponde più (Esplora file chiuso a forza o in crash: il sistema la smonta
  scheda per scheda) il suo stato resta congelato, così viene ricordata intera. La sessione si scrive
  su un file temporaneo e poi lo sostituisce; se `session.txt` è aperto in quel momento da un altro
  programma (antivirus, indicizzazione) la sostituzione fallisce (errore 5): si riprova subito e poi a
  ogni controllo finché non riesce, invece di lasciare su disco la sessione vecchia.

### Schede verticali (la mod dentro Esplora file)

Stessa tecnica di VTabsNotepad. Con l'impostazione predefinita di Windows le finestre di Esplora file
vivono nello **stesso explorer.exe della barra delle applicazioni e del desktop**: per questo la mod è
facoltativa e protetta (vedi "Sicurezza della mod").

- **Iniezione** (`ModManager.cpp`, nella tray): con la funzione attiva, la tray carica la mod
  (`CreateRemoteThread` + `LoadLibraryW`, da una copia della DLL in `%LOCALAPPDATA%\VTabsExplorer\runtime\`)
  nei processi explorer.exe che hanno una finestra di Esplora file visibile, assestata da 1,5 s e non
  velata (una sessione in ricostruzione non va disturbata). Nessuna finestra aperta, nessuna iniezione:
  dopo l'accesso a Windows la mod entra con la prima finestra, senza riavviare nulla. Le finestre
  aperte dopo le gestisce la mod da sola.
- **Aggancio** (`ModCore.cpp` + `Discovery.cpp`, dal 2026-11-05 **senza la diagnostica XAML**, che teneva
  vive per sempre le finestre chiuse: vedi "Problema risolto" sotto Stato). La mod sostituisce due voci
  della tabella dei metodi (vtable) condivisa da tutte le istanze di due classi WinUI, slot di interfacce
  WinRT pubbliche (ABI stabile), installate una volta sul thread della prima finestra con l'intestazione
  pronta (gancio `WH_CALLWNDPROC` limitato a quel thread + un messaggio nostro):
  - `IFrameworkElementOverrides::OnApplyTemplate` di `TabView`: ogni barra delle schede nuova si presenta
    da sé, sul suo thread, quando applica il modello (prima di comparire);
  - `IAutomationPeerOverrides::GetAutomationControlTypeCore` di `TabViewItemAutomationPeer`: per le
    finestre già aperte quando entra la mod, una ricerca UI Automation delle schede sull'isola
    dell'intestazione fa chiamare questo metodo sul thread della finestra; dal "peer" si risale alla scheda
    e alla barra. Fuori da una ricerca nostra l'aggancio passa oltre. La ricerca si ripete come rete di
    sicurezza (ogni 5 s al più) per le finestre visibili non ancora gestite.

  Gli agganci chiamano prima il metodo originale e tengono la barra solo con un riferimento debole.
  `Microsoft.UI.Xaml.dll` usa CFG, non XFG (Guard Flags `0x10017500`): un puntatore a una nostra funzione
  nella vtable è un bersaglio valido. Per ogni barra delle schede dell'intestazione (`TabView` dentro
  `FileExplorerTabControl`) nasce un `VerticalTabs` sul thread di quella finestra.
- **Pannello** (`VerticalTabs.cpp`): una nostra isola XAML (`DesktopWindowXamlSource`, Mica Alt come la
  barra del titolo) con componenti WinUI di sistema, legata alla barra schede nativa, che resta viva ma
  invisibile (opacità 0, niente clic). Elenco e ordine dalla raccolta delle schede (eventi
  `VectorChanged`), selezione dal `TabView` (eventi `SelectionChanged`), titoli e icone dalle schede
  native realizzate (eventi `PropertyChanged` dei loro modelli); un controllo completo ogni 4 s fa da
  rete di sicurezza. I comandi passano dai controlli nativi: selezione con `SelectedIndex`, chiusura e
  "nuova scheda" premendo (UI Automation) i loro pulsanti, menu contestuale con il `ContextFlyout` della
  scheda nativa aperto nel punto del clic, riordino togliendo e reinserendo la scheda nella raccolta
  di Esplora file (`IVector<IInspectable>`), come fa la barra orizzontale quando vi si trascina una scheda.
- **Spazio** (`WindowLayout.cpp`): il contenuto di ogni scheda è una finestra `ShellTabWindowClass` che
  Esplora file mette sotto l'intestazione a tutta larghezza e riposiziona a ogni ridimensionamento o
  cambio scheda; un subclass ne corregge ogni spostamento in `WM_WINDOWPOSCHANGING` (niente
  sfarfallio): il bordo sinistro passa a destra della colonna. Il rettangolo chiesto da Esplora file
  resta il "naturale", per ripristinare tutto e dare al pannello la stessa altezza. Le schede nuove si
  agganciano appena create (`WM_PARENTNOTIFY`).
- **Colonna in cima alla finestra** (come Chrome; `WindowLayout.cpp`, `VerticalTabs::UpdateCaptionRegion`):
  - l'isola XAML dell'intestazione di Esplora file (`Microsoft.UI.Content.DesktopChildSiteBridge` figlia
    della finestra, l'unica senza il contrassegno della mod) ha lo stesso subclass del contenuto: comincia
    dal bordo destro della colonna, e il suo XAML si adatta da sé alla larghezza nuova (indirizzo e ricerca
    più corti, voci della barra dei comandi nel menu "Altre opzioni" quando non ci stanno). I suoi menu
    (finestre a sé) compaiono sotto i loro pulsanti. Si riconosce solo nel controllo delle figlie, mai alla
    creazione: le isole della mod ricevono il contrassegno subito dopo essere nate;
  - la colonna va dalla cima dell'intestazione (3 px sotto il bordo: sopra c'è il bordo superiore che
    ridimensiona) al fondo del contenuto; la barra delle cartelle comincia anche lei dal suo bordo;
  - la zona di trascinamento (`TITLE_BAR_SCAFFOLDING_WINDOW_CLASS`, la finestra di
    `InputNonClientPointerSource` che risponde `HTCAPTION`, i bordi e i pulsanti riduci/ingrandisci/chiudi)
    copre tutta la riga del titolo a destra della colonna, dove c'erano le schede orizzontali;
  - in cima alla colonna la **fascia del titolo**, alta quanto quella zona (40 px al 125%), ha i pulsanti
    del pannello centrati accanto a quelli della finestra; separatore e divisore cominciano dove comincia
    il contenuto (sopra, colonna e intestazione sono la stessa Mica);
  - il mouse delle isole XAML **non passa da `WM_NCHITTEST`** (né il subclass dell'isola né un aggancio
    `WH_MOUSE` del thread vedono il clic vero: `HTTRANSPARENT` funziona solo con `SendMessage`). Per
    lasciare la fascia alla finestra l'isola del pannello ha una **regione di finestra** che la esclude,
    salvo i pulsanti, tenuti fino al fondo della fascia (rifatta dopo ogni layout, solo se cambia): lì sotto
    c'è la finestra principale, il cui `WM_NCHITTEST` (subclass) risponde `HTCAPTION`. Trascinamento, doppio
    clic e menu di sistema sono quelli di Windows. La regione taglia anche il disegno dell'isola: nel buco
    si vede la finestra sotto, che in Esplora file lì ha solo la sua Mica (uguale; nel Blocco note sotto
    "comprimi" c'è la sua icona, per questo i pulsanti si tengono fino al fondo della fascia);
  - senza l'isola dell'intestazione (non trovata) la colonna resta sotto l'intestazione come prima e la
    zona di trascinamento va fino al bordo sinistro;
  - **riga del titolo nascosta**: `VerticalTabs::UpdateHeaderRows` legge dal XAML dell'intestazione dove comincia
    la barra degli indirizzi (`NavigationBarControl`, 38,4 DIP dalla cima dell'isola: è l'altezza della riga del
    titolo) e quanto è alta (48 DIP), e le passa al layout (`SetHeaderRows`, in DIP: al cambio di scala si
    ricalcola da sé). L'isola dell'intestazione sale di quell'altezza: la riga del titolo resta sopra la finestra,
    e una regione dell'isola (`UpdateHeaderRegion`) taglia anche i 3 px in cima, dove la finestra ridimensiona
    (`HTTOP`). Barra delle cartelle e contenuto salgono con lei. La barra degli indirizzi lascia a destra 154 DIP
    (i tre pulsanti della finestra da 46 DIP più 16 liberi): margine destro di `NavigationBarControl` tenuto con
    un'animazione ferma (`HoldEnd`), come gli sfondi dell'intestazione; fermandola torna quello di Esplora file. La
    zona di trascinamento si stringe su quei 154 DIP, dalla cima della finestra al fondo della barra degli indirizzi:
    risponde per i pulsanti, per il tratto libero (`HTCAPTION`) e per l'angolo. La fascia del titolo della colonna
    è alta quanto la barra degli indirizzi, con i pulsanti allineati ai suoi. Il `TabView` nascosto resta vivo e
    realizzato (titoli, icone e menu contestuale delle schede come prima);
  - **pulsanti della finestra alti quanto la barra degli indirizzi** (`CaptionButtons.cpp`, lo stesso componente in
    VTabsNotepad). I pulsanti di DWM sono alti 30 DIP e non si allungano. Quelli di DWM si nascondono con le opzioni del tema
    della zona non client: `SetWindowThemeAttribute(WTA_NONCLIENT)` con il flag non documentato `0x10000`, lo stesso che usa
    il Windows App SDK (trovato nel disassemblato di `Microsoft.UI.Windowing.Core.dll`: flag `0x10007`, maschera `0x1000F`,
    tolto con flag 0). Togliendo il flag tornano com'erano (`DWMWA_CAPTION_BUTTON_BOUNDS` di nuovo largo); stile, Mica e
    menu di sistema non cambiano. Al loro posto una finestra figlia **layered** (alfa per pixel, sotto si vede la Mica),
    disegnata con Direct2D/DirectWrite:
    - glifi di Segoe Fluent Icons a 10 DIP arrotondati per difetto (12 px al 125%), confrontati pixel per pixel con quelli
      di DWM: antialias lineare (gamma 1, senza contrasto aumentato), riduci a icona con l'hinting "GDI classico" (una riga
      di pixel), gli altri senza; centrati sulla riga della barra degli indirizzi a mezzo pixel come quelli di DWM;
    - colori dei pulsanti di Windows 11 (fondo `#0FFFFFFF` al passaggio, `#0AFFFFFF` premuto, chiudi `#C42B1C`; glifo al
      40% con la finestra non attiva), tema dalla finestra (`DWMWA_USE_IMMERSIVE_DARK_MODE`, si legge), ricontrollato;
    - **alfa 1/255 ovunque**: i pixel del tutto trasparenti di una finestra layered non ricevono il mouse (passa alla
      finestra sotto): senza, i pulsanti rispondevano solo sopra i glifi;
    - all'hit test risponde `HTMINBUTTON`/`HTMAXBUTTON`/`HTCLOSE` come il Terminale di Windows (così Windows 11 mostra i
      layout di ancoraggio, se attivi). Il clic lo segue con la cattura del mouse: al rilascio sullo stesso pulsante
      `WM_SYSCOMMAND` alla finestra principale; premuto e trascinato fuori non fa niente. Tasto destro: menu di sistema
      (`WM_POPUPSYSTEMMENU`). Riga di 1 px in cima `HTTRANSPARENT` (finestra non ingrandita): lì ridimensiona chi sta sotto;
    - dalla cima della finestra al fondo della barra degli indirizzi (63 px al 125%), 46 DIP l'uno; da ingrandita dalla
      cima dello schermo (54 px);
    - Esplora file rimette in cima la zona di trascinamento a ogni layout (`SetWindowPos HWND_TOP`): un clic in quel
      momento la trascinava invece di ingrandire. Ora la sua richiesta di andare in cima la mette subito sotto i pulsanti
      (`hwndInsertAfter`), e `PositionPanel` li rimette comunque in cima;
    - appena ripristinata da icona l'area client è vuota per un attimo: lì non si tocca niente (prima i pulsanti si
      toglievano e rimettevano);
    - si tolgono (tornano quelli di DWM) spegnendo le schede verticali, senza riga del titolo nascosta e con il contrasto
      elevato.

    Provato prima il Windows App SDK (`AppWindow.TitleBar`: `ExtendsContentIntoTitleBar` più `PreferredHeightOption = Tall`,
    pulsanti di 48 DIP): funziona, ma la sua finestra dei pulsanti finiva sotto la zona di trascinamento di Esplora file e,
    rimettendo `ExtendsContentIntoTitleBar = false`, restava una barra nera sopra la barra degli indirizzi fino alla
    chiusura della finestra: scartato;
  - **zona vetro** (`Glass.cpp`): Esplora file estende la cornice di sistema nell'area client
    (`DwmExtendFrameIntoClientArea`) fino al fondo dell'intestazione, così dietro le sue barre si vede la Mica. Lì DWM
    compone i pixel con il loro alfa, e il contenuto salito, che disegna senza alfa, si sommava allo sfondo. Ne usciva
    una fascia più chiara di 48 px in cima: `#191919` + `#202020` = `#393939`, nel riquadro di spostamento e sotto
    l'intestazione delle colonne, che invece è una finestra a sé e resta scura. La funzione la importa
    `ExplorerFrame.dll` con il caricamento ritardato. La mod aggancia la voce nella sua tabella delle importazioni
    ritardate (e in quella di `ieframe.dll`, se è caricata), chiamando la funzione vera attraverso le proprie
    importazioni: il thunk del caricamento ritardato riscriverebbe la voce. L'aggancio ricorda le richieste e, con la
    riga del titolo nascosta, limita il margine superiore all'inizio del contenuto. Con la barra delle cartelle accesa
    il limite è sotto di lei, che resta nel vetro come l'intestazione. Esplora file la richiama quando la finestra
    viene ingrandita o ripristinata (170 px al 125%, ridotti a 122 o 165). Spente le schede verticali torna la zona di
    Esplora file. Come nel Blocco note, anche `CabinetWClass` non ha `WS_CLIPCHILDREN`; con comprimi/espandi
    del pannello però non si è vista nessuna fascia scura.
- **Intestazione del colore della barra del titolo** (`UnifiedHeader.cpp`): l'isola dell'intestazione
  ha lo sfondo trasparente, quindi nella barra del titolo si vede lo sfondo della finestra (Mica Alt).
  Le due fasce sotto sono dipinte da Esplora file con pennelli suoi:
  - fascia degli indirizzi: `Grid #NavigationBarControlGrid` (#733A3A3A nel tema scuro, #B3FFFFFF nel
    chiaro: lo strato "su Mica");
  - fascia dei comandi: il `Grid` dentro `CommandBarControl_Wave1` (#FF191919 / #FFFFFFFF, con le linee
    sopra e sotto), con dentro `CommandBar #FileExplorerCommandBar` e `#FileExplorerSecondaryCommandBar`.

  Su quei quattro sfondi, e sulle due linee sotto la striscia delle schede (che resterebbero a metà),
  la mod tiene un'animazione ferma (Storyboard a durata zero, `HoldEnd`) con un pennello trasparente.
  Le animazioni hanno la precedenza sul valore dell'elemento senza cancellarlo. Sostituire il valore
  (`SetValue`) gli farebbe invece perdere il legame con la risorsa di tema: dopo un cambio di tema,
  spegnendo la funzione, la fascia dei comandi tornava scura nel tema chiaro (verificato).
  Le parti si cercano per nome. Ogni 4 s si controlla che siano ancora quelle, perché Esplora file può
  ricrearle, e che le animazioni tengano. Con le schede orizzontali non si applica: la scheda attiva si
  "aggancia" alla barra degli indirizzi, che deve restare del suo colore.
- **Riquadro di spostamento in alto** (`NavItems.cpp`, `NavBar.cpp`):
  - **Voci** (`NavItems`, un thread a sé, a priorità bassa, per tutto il processo). Composte come le
    compone Esplora file, verificato voce per voce con l'albero del riquadro (una radice nascosta
    "Desktop" e sotto le voci visibili):
    - gli elementi del Desktop "fissati nell'albero" (`System.IsPinnedToNameSpaceTree`: Home, Galleria,
      OneDrive, Questo PC, Rete, Linux), ordinati con il `SortOrderIndex` del loro CLSID (Home 0x40,
      Galleria 0x41, Rete 0x58, Linux 0x77; Questo PC, senza valore, 0x50);
    - le cartelle aggiunte ad Accesso rapido (`System.Home.IsPinned`), nel loro ordine, a 0x48.
    
    Le aggiunte si leggono da "Posizioni frequenti" (`shell:::{3936E9E4-…}`), che ha le stesse cartelle
    senza i file recenti: circa 30 ms invece dei 200-400 di Accesso rapido. Gli elementi del Desktop
    costano (si passa da tutte le cartelle sul desktop dell'utente) e cambiano di rado: si rileggono ogni
    10 minuti. Le icone (`IShellItemImageFactory`, alla dimensione in pixel della finestra) restano in
    memoria.
    
    Si rilegge quando la shell notifica cambiamenti su Accesso rapido o Posizioni frequenti
    (`SHChangeNotifyRegister`), dopo un comando del menu contestuale, quando una finestra torna in primo
    piano (al più ogni 30 s) e comunque ogni 5 minuti.
  - **Barra** (`NavBar`): una nostra isola XAML per finestra, Mica Alt, alta 34 DIP, con pulsanti WinUI
    trasparenti (icona 16 DIP e nome). "»" apre un `Flyout` con un elenco di nostri pulsanti: un
    `MenuFlyout` non si lascia trascinare. L'elenco sta in uno `ScrollViewer` alto al più fino al fondo
    dello schermo. Il testo è a 12 DIP nella barra e negli elenchi (`kTextSize`, come Chrome); le righe
    degli elenchi restano alte almeno 32 DIP. Fino al 2026-11-06 gli elenchi avevano il 14 dei menu di
    Windows 11, che l'utente vedeva più grande della barra.
  - **Spostamenti** (trascinamento delle cartelle aggiunte):
    - premuto il tasto su una cartella aggiunta, oltre la soglia di Windows (`SM_CXDRAG`) il pulsante
      perde la cattura del puntatore, quindi niente clic, e la prende la radice della barra o
      dell'elenco;
    - la radice segue il puntatore anche sopra l'altra finestra (barra ↔ elenco). Una linea d'accento
      segna il posto, solo fra le cartelle aggiunte, che sono di seguito; fermandosi su "»" l'elenco si
      apre;
    - al rilascio la voce si sposta subito a vista e `NavItems::MovePinned` passa lo spostamento al
      thread delle letture;
    - lì `IAutomaticDestinationList::PinItem` lo fa in Windows, con l'indice dell'ancora (la voce prima
      della quale va, o quella dopo cui va) chiesto a `IsPinned`;
    - poi si rilegge e si pubblica comunque una versione nuova: se lo spostamento non fosse riuscito, la
      barra torna all'ordine vero;
    - il comando di prova `navbar move <i> <posto>` fa lo stesso spostamento di un rilascio, e
      `navbar dragdebug on|off` registra pressioni, movimenti e rilasci.
  - **Aperture**:
    - nella scheda attiva: `IShellBrowser::BrowseObject` sul browser della scheda, che si ottiene dalla
      sua `ShellTabWindowClass` con il messaggio della shell `CWM_GETISHELLBROWSER` (`WM_USER+7`, nello
      stesso processo);
    - in una scheda nuova: il verbo `opennewtab` del menu della cartella, con quel browser come sito (la
      scheda si apre nella stessa finestra);
    - sempre la **cartella vera**, ricavata dal nome di analisi (`SHParseDisplayName`), non l'elemento
      dentro "Posizioni frequenti" da cui la barra la conosce. Aprendo quello, Esplora file registra la
      visita con la sua copia dell'elenco delle fissate e, se la copia è vecchia, riscrive in Windows
      l'ordine di prima: uno spostamento fatto dalla barra si perdeva al primo clic su una cartella (visto
      col mouse vero il 2026-11-04 e riprodotto: con la cartella vera il file delle fissate non cambia). I
      menu restano quelli dell'elemento di Accesso rapido: "Rimuovi da Accesso rapido" funziona solo lì.
  - **Caselle di testo** (`TextInput.cpp`): il ciclo dei messaggi di Esplora file passa ogni tasto dalle
    sue scorciatoie prima di consegnarlo, e per le nostre isole XAML non sa che c'è una casella di testo:
    Backspace diventava "Indietro" (la scheda tornava alla cartella di prima, la lettera restava) ed Esc
    non arrivava. Finché una nostra casella ha il fuoco (il nome del gruppo; prima anche "Cerca nelle schede"
    delle schede verticali, tolta) un hook `WH_GETMESSAGE` del thread della finestra consegna da sé `WM_KEYDOWN`,
    `WM_KEYUP` e `WM_CHAR` diretti alla finestra d'ingresso dell'isola, e lascia al ciclo un `WM_NULL`. I
    tasti di sistema (Alt, F10...) restano a Esplora file.
  - **Menu contestuale nuovo** (`ModernMenu.cpp`): lo apre come il riquadro di sistema
    (`CNscTree::DoCuratedContextMenu` di ExplorerFrame.dll), con il "presenter" di
    Windows.UI.FileExplorer.dll. Le interfacce **non sono documentate**: le ho ricostruite dai simboli
    pubblici di Microsoft (build 26300), con `dumpbin /disasm` sulle DLL e i loro PDB:
    - `IContextMenuPresenter` {37a472f7-63cf-4ccf-a88b-5231a3c7d8b6}, dalla classe "File Explorer Context
      Menu" {86ca1aa0-…} (o {51668040-…} se la funzione 60813048 è accesa, come fa il riquadro). Metodi:
      `Initialize(TRUE, ospite, finestra, 2)`, `PrepareForContextMenu(0x10, sito, punto, menu, 11, 0x7FFF,
      TRUE, nullptr, GUID_NULL, elemento)`, `DoContextMenu(hmenu, flag, GUID_NULL)` (l'HMENU passa al
      presenter), `DismissContextMenu`, `IsContextMenuOpen`, `Invoke(id)`, `IsReadyForNewContextMenu`;
    - l'ospite (nostro) implementa `IInvokeContextMenuCommand` {9a19ddcf-9ed4-4a18-89de-c3b4dd1d7ae3}:
      `Invoke(menu, elementi, hmenu, id, punto)`, `OnContextMenuDismiss`, `DoExpandedContextMenu` ("Mostra
      altre opzioni"), `SetFocus`. Il presenter lo tiene senza riferimento;
    - il presenter esegue la voce scelta mandando `FILE_EXPLORER_CONTEXTMENU_INVOKEMENUITEM` (messaggio
      registrato, wParam = id) alla finestra dell'ospite (`IOleWindow::GetWindow`): una nostra finestra solo
      messaggi, che chiama `Invoke(id)` sul presenter, che a sua volta chiama l'ospite;
    - come quelli di Windows, un presenter **per scheda**, con la scheda (`ShellTabWindowClass`) come
      finestra: crea un'isola XAML figlia (0×0). Sulla finestra principale confondeva chi cerca l'isola
      dell'intestazione come "la più in alto". Si chiude con `IClosable::Close` quando la scheda va via
      (`WM_DESTROY`, prima delle figlie) o cambiando scheda;
    - flag di `DoContextMenu`: 8 = clic destro, 2 = tocco, 4 = penna (dal tipo di puntatore di
      `RightTapped`);
    - se il menu di prima si sta chiudendo, `IsReadyForNewContextMenu` dice di no e il presenter, chiuso,
      manda `WM_CONTEXTMENU` alla scheda nel punto: lo prende il subclass e la barra riapre il menu lì;
    - ogni passo controlla il risultato: se la classe o l'interfaccia non ci sono, o qualcosa fallisce, si
      apre il menu classico.
  - **Il menu della voce** (`MenuArray.cpp`): come il riquadro, l'unione di due menu della shell, quello
    della cartella e quello "di sfondo" della cartella (`IShellFolder::CreateViewObject`, da cui vengono
    "Nuovo" e simili), senza verbi doppi. È il `CContextMenuOnContextMenuArrayNoDuplicateVerbs` di
    ExplorerFrame rifatto:
    - ogni menu ha la sua fascia di id;
    - comandi, verbi e messaggi dei sottomenu vanno al menu che ha l'id;
    - sito e restrizioni vanno a tutti.

    Per il menu nuovo, come il riquadro:
    - `SetMenuRestrictions(DFMR_OPTIN_HANDLERS_ONLY | 0x2000)`: le estensioni che non supportano il menu
      nuovo restano in "Mostra altre opzioni";
    - `QueryContextMenu` con `CMF_EXPLORE | CMF_ASYNCVERBSTATE | 0x8000`, senza `CMF_CANRENAME`;
    - via "link" e "open".
  - **Menu classico** (Maiusc+clic destro, "Mostra altre opzioni"): la stessa unione, con
    `TrackPopupMenuEx`, senza "Crea collegamento". "Apri" resta, perché il riquadro la sostituisce con
    "Espandi". I messaggi dei sottomenu disegnati dalla shell passano dal subclass della finestra principale
    a `IContextMenu3`.
  - **Cartelle trascinate da fuori** (`NavDrop.cpp`, dalla 1.3.0), come un link sulla barra dei segnalibri di
    Chrome:
    - l'isola della barra ha un bersaglio OLE (`RegisterDragDrop`; il ponte dell'isola di WinUI non ne aveva
      uno suo). Il trascinamento XAML non arriva alle isole aggiunte da noi, un bersaglio OLE sì;
    - dall'oggetto trascinato (`SHCreateShellItemArrayFromDataObject`; dalla 1.4.0, se non ne esce niente, i
      percorsi di `CF_HDROP`, l'unico formato di molti programmi che non sono la shell) valgono solo le
      cartelle: `SFGAO_FOLDER` senza `SFGAO_STREAM`, quindi niente file né archivi .zip;
    - il bersaglio è quello del trascinamento interno (`HitTestScreen`, `ShowTarget`): la linea nel blocco
      delle cartelle aggiunte, il gruppo evidenziato sul suo quarto centrale;
    - **elenchi** (dalla 1.4.0): fermi su un gruppo o su "»" il suo elenco si apre come trascinando dalla barra
      (`OpenHoveredLevel`, dal timer di 16 ms del trascinamento; `OnSwitchTimer` intanto non cambia elenco). La
      finestra di ogni elenco aperto (`Microsoft.UI.Content.PopupWindowSiteBridge`, di proprietà della finestra
      principale, senza un bersaglio suo) riceve lo stesso bersaglio OLE finché l'elenco è aperto
      (`AttachLevelDropTargets`: la finestra visibile del nostro thread sul centro dell'elenco, grande come
      lui; tolto in `OnLevelClosed`). Passando dalla barra a un elenco (o tra due elenchi) Windows dà
      `DragLeave` e subito `DragEnter`: il trascinamento continua. Da un altro thread o processo l'oggetto
      dati arriva ogni volta come un oggetto COM diverso (provato: il confronto di identità fallisce), quindi
      conta l'ordine: un'entrata dopo un'uscita è lo stesso trascinamento (Windows ne fa uno alla volta) e si
      prende l'oggetto nuovo. Fuori da barra ed elenchi il trascinamento finisce dopo 200 ms, e gli elenchi si
      chiudono, a meno che il puntatore resti lì accanto (16 DIP) col tasto premuto: lo spazio tra la barra
      e un elenco è di Esplora file. Al rilascio gli elenchi si chiudono subito dopo la chiamata OLE (il
      rilascio può arrivare proprio al bersaglio della finestra che si chiude). Durante il trascinamento la
      barra non si ricostruisce, come per quello interno;
    - l'effetto è `DROPEFFECT_LINK` (o `COPY` se la sorgente non ammette il collegamento), mai `MOVE`:
      Esplora file sposterebbe la cartella;
    - l'immagine del trascinamento di Windows resta anche sopra la barra (`IDropTargetHelper`). Accanto c'è
      "Aggiungi ad Accesso rapido" o "Aggiungi a <gruppo>" (`CFSTR_DROPDESCRIPTION`), tolto uscendo e al
      rilascio;
    - al rilascio una cartella già aggiunta si sposta lì (`PlaceNode`, come dalla barra). Le nuove si
      aggiungono dal thread delle letture (`NavItems::PinFolders`) con `IAutomaticDestinationList::PinItem`,
      prima della prima cartella aggiunta dal punto di rilascio in poi; in fondo se non ce n'è, e per i
      gruppi. Quando la lettura nuova le porta (entro 15 s) vanno al loro posto con `PlaceNode`: nel gruppo,
      o accanto a un gruppo, e l'ordine di Windows segue come per i riordini;
    - Windows elenca al più **50 cartelle aggiunte** (Accesso rapido e "Posizioni frequenti", da cui legge la
      barra; provato con 55 aggiunte, tutte raggiungibili: 50 elencate). Oltre, le ultime non si vedono, nella
      barra come nel riquadro di sistema e nella Home.
  - **Gruppi** (`NavGroups.cpp`, `NavBar.cpp`):
    - le voci si riconoscono da una chiave: `f:` più il nome di analisi in minuscolo per le cartelle,
      `g:` più un numero per i gruppi. Un gruppo ha nome e contenuto (chiavi, in ordine); una cartella sta
      al più in un gruppo, e quelle in nessun gruppo restano al primo livello;
    - il primo livello del blocco delle cartelle aggiunte (`TopLevel`): le cartelle non raggruppate
      nell'ordine di Windows, e ogni gruppo prima della cartella che lo seguiva l'ultima volta che la barra
      ha disposto le voci (`top`, salvato). Se quella non c'è più si passa alla successiva, e senza
      nessuna il gruppo va in fondo. Così i gruppi seguono le cartelle vicine anche se l'ordine cambia
      altrove (Home, riquadro di sistema);
    - ogni operazione (sposta, crea, rinomina, elimina, dimentica) parte dal primo livello come lo si
      vede, lo cambia, lo salva come `top` e pubblica uno stato nuovo, immutabile, con una versione nuova.
      Le finestre dello stesso processo lo prendono al controllo periodico. Uno stato letto dal file si
      rimette in ordine (`Normalize`): id unici, ogni voce in un posto solo, niente cicli;
    - l'ordine di Windows (`NavOrder.cpp`) è la barra letta in profondità: il primo livello e, al posto di
      ogni gruppo, il suo contenuto (sottogruppi compresi). Si adegua con il minimo di spostamenti: restano
      ferme le cartelle della sottosequenza crescente più lunga, e le altre, dall'ultima alla prima, vanno
      ognuna prima della successiva (`NavItems::MovePinned`, cioè `PinItem`). A vista lo spostamento è subito.
      Dopo un'operazione il primo livello è quello voluto (`top`: una voce appena spostata non è ancora lì in
      Windows); all'avvio, una volta per processo, è quello che si vede, così un riordino delle cartelle
      libere fatto altrove (Home) resta e si spostano solo quelle dei gruppi. Con la barra spenta lo fa
      `VerticalTabs::KeepNativeOrder` dal controllo periodico della finestra, solo se ci sono gruppi;
    - il riquadro di sistema delle schede aperte non rilegge mai l'ordine: si riordinano le voci di primo
      livello del suo albero (`TVM_SORTCHILDRENCB` sulla radice nascosta) come le voci lette, per nome (i nomi
      doppi, come le cartelle col nome invisibile, nell'ordine in cui compaiono; una voce sconosciuta resta
      dopo quella che la precede). Si fa a ogni ricostruzione della barra (il riquadro è nascosto), quando la
      barra va via e, con la barra spenta, a ogni lettura nuova;
    - file: `navbar-groups.json` (JSON con `Windows.Data.Json`), scritto a parte e poi messo al posto di
      quello vecchio (`MoveFileEx`), mai a metà. Il processo della barra delle applicazioni usa quello
      dell'utente; ogni altro explorer.exe (le finestre di prova isolate) `navbar-groups-test.json`. Un
      file illeggibile si mette da parte (`.illeggibile-…`) e non si sovrascrive;
    - elenchi: ogni pulsante di un gruppo (nella barra o in un elenco) ha il suo `Flyout` con i nostri
      pulsanti, come "»", riempito all'apertura. Quelli aperti stanno in una pila, uno per profondità: un
      elenco nuovo chiude quelli della sua profondità e più interni. I sottogruppi sono flyout aperti da
      un pulsante dentro un altro flyout, e WinUI li tiene aperti insieme. Fermandosi su una riga
      (400 ms) il sottogruppo si apre senza prendere il fuoco (`FlyoutShowMode::Transient`). Con un elenco
      aperto dalla barra i pulsanti della barra non ricevono il puntatore: un timer (`OnSwitchTimer`, ogni
      16 ms, solo finché c'è un elenco aperto) guarda quale voce c'è sotto il puntatore e, appena è un
      altro gruppo o "»", apre il suo elenco al posto di quello aperto (come Chrome). Dal 2026-11-06 senza
      sosta (prima: timer di 100 ms e 150 ms fermi sopra, più le animazioni, il ritardo che si notava) e
      con `AreOpenCloseAnimationsEnabled(false)` sui due elenchi, così quello vecchio sparisce e quello
      nuovo compare nello stesso momento; le animazioni tornano quando l'elenco si chiude (aperto col clic
      resta animato). Il pulsante dell'elenco aperto (`SetOpenHighlight`) prende come sfondo quello del
      passaggio del mouse (`ButtonBackgroundPointerOver` = `ControlFillColorSecondary`: `#15FFFFFF` nel
      tema scuro, `#80F9F9F9` in quello chiaro), rimesso trasparente alla chiusura; se il cursore non è più
      sul pulsante torna anche lo stato visivo "Normal" (con l'elenco aperto il pulsante non sa che il
      puntatore è uscito);
    - aggiornamenti: se le voci della barra non cambiano (stesse chiavi, stesso nome e icona) i pulsanti
      restano, e gli elenchi aperti si riempiono di nuovo solo se il loro contenuto è cambiato. L'aspetto di
      un gruppo è solo il nome: il suo contenuto (nel suggerimento, "2 cartelle") si aggiorna sul pulsante che
      c'è (`RefreshGroupTooltip`). Prima del 2026-11-06 era parte dell'aspetto, così un conteggio cambiato
      rifaceva la barra e chiudeva l'elenco in cui si era appena lasciata la voce (i riordini no);
    - trascinamento: il bersaglio (`HitTest`) si cerca prima negli elenchi aperti, dal più interno, poi
      nella barra. Sul quarto centrale di un gruppo la voce entra (il pulsante va nello stato
      "PointerOver"), altrove va prima o dopo. Restando fermi su un gruppo o su "»" (600 ms) si apre
      l'elenco. Se è della profondità dell'elenco da cui parte la voce (o meno profondo), quell'elenco si
      chiude, perché ce n'è uno per profondità. Fino al 2026-11-06 questo caso era escluso, perché il
      trascinamento finiva con l'elenco: il puntatore è catturato dalla sua radice, e chiuso l'elenco la cattura si
      perde. Ora il trascinamento prima **si stacca** (`DetachDrag`):
      - la cattura XAML della radice dell'elenco si toglie mentre l'elenco c'è ancora, e la sua perdita non
        annulla più niente;
      - da lì cursore e tasto si leggono ogni 16 ms (`GetCursorPos`, `GetAsyncKeyState` del tasto principale,
        Esc solo con la finestra in primo piano), e il bersaglio si calcola dal punto dello schermo
        (`HitTestScreen`, coordinate della radice della barra trasformate verso ogni elemento come fa
        `GetCurrentPoint`).
      Serve perché la cattura "vera" del mouse nelle isole di WinAppSDK non è quella XAML: la tiene
      l'`InputPointerSource` della finestra dove è avvenuta la pressione. Nel sorgente di WinUI,
      `CXamlIslandRoot::SetPointerCapture` e `InputSiteAdapter::SetPointerCapture` sono funzioni vuote, e
      `WindowedPopupInputSiteAdapter::OnPointerMessage` scarta l'input di un popup chiuso. Quindi, chiuso
      l'elenco, movimenti e rilascio possono non arrivare più a nessun gestore XAML. Gli eventi XAML che
      arrivano ancora (sulla barra o su un elenco) restano usati: posizione esatta, rilascio subito;
    - "Sposta nel gruppo" / "Togli dal gruppo" nel menu classico di una cartella: voci aggiunte all'HMENU
      della shell con id da 0x7F00 (la shell usa quelli prima). Il menu nuovo non le mostrerebbe: il
      presenter costruisce un "mini menu" da quello classico (`TryCreateMiniMenuFromClassic`) tenendo solo
      i verbi di Windows ammessi per quel punto (`GetAllowedVerbs` + `FilterClassicMenu`) e le estensioni
      installate. Provato: aggiunte all'HMENU passato al presenter, non comparivano;
    - **schede nuove una alla volta** ("Apri tutte", clic centrale, `QueueNewTab`): due schede create una
      sopra l'altra con il verbo `opennewtab` fanno cadere Esplora file (0xC0000005 in
      `CShellBrowser::QueryService`, vedi note tecniche). La prossima parte solo quando la scheda di prima
      ha la sua vista dei file (`SHELLDLL_DefView`), più 300 ms, o dopo al più 4 s. Ci passano anche i
      clic centrali, e "Apri in una nuova scheda" scelta dal menu della barra fa aspettare la coda;
    - comandi di prova `navbar group …` (per nome; vedi `DevGroupCommand` in `NavBar.cpp`): creare,
      spostare, aprire gli elenchi, i menu, il riquadro del nome, e `drop`, che calcola il bersaglio come
      il trascinamento vero con il punto dato su un pulsante (frazione della larghezza o dell'altezza).
      `hover <gruppo>` (o `hover >>` per "»") fa vedere a `OnSwitchTimer` il cursore al centro di quel
      pulsante della barra finché resta aperto un elenco (`hover off`: di nuovo il cursore vero), e il
      cambio si scrive nel registro con i millisecondi dal comando; `dump` dice anche quali pulsanti sono
      evidenziati, se l'elenco aperto ha le animazioni e il contenuto scritto nel suggerimento di ogni gruppo
      della barra (letto dal pulsante: UI Automation non espone i suggerimenti). `drag <voce>`, `dragto <x>|<y>` (pixel fisici dello
      schermo), `dragup`, `dragesc`: un trascinamento vero, con timer, elenchi che si aprono e stacco, guidato
      da punto e tasto di prova invece del mouse.
  - **Spazio** (`WindowLayout`):
    - il contenuto delle schede (e la colonna) scende sotto la barra;
    - il riquadro di spostamento nativo si nasconde **dentro il contenuto** della scheda (dal 2026-11-05).
      In `DUIViewWndClassName > DirectUIHWND` ogni riquadro è una finestra `CtrlNotifySink` che DirectUI
      posiziona: quella del riquadro di spostamento (con `NamespaceTreeControl`), quella della vista dei file
      (con `SHELLDLL_DefView`), poi anteprima e dettagli, nascoste se spente.
      - **Cosa fa la mod.** La sink del riquadro va tutta a sinistra del bordo, fuori vista, perché il
        contenuto la taglia. La sink visibile subito alla sua destra, cioè la vista, si allarga fino al
        bordo sinistro. Anche le finestre interne che occupano tutta la sink (`SHELLDLL_DefView`) si
        allargano con lei: Esplora file le dimensiona sul posto che DirectUI ha dato alla sink, non sulla
        sink allargata. Tutte sono seguite con un subclass (`SinkProc`, `WM_WINDOWPOSCHANGING`): la
        richiesta di DirectUI resta il rettangolo "naturale".
      - **La regione della vista.** DirectUI dà alla sink della vista una regione grande quanto il suo posto
        (0..773, ridata a ogni layout insieme alla posizione). Sulla vista allargata la si sposta e le si
        aggiunge la striscia a sinistra; togliendo la barra torna la copia di quella di DirectUI.
      - **Perché non più lo spostamento del contenuto.** Prima si spostava tutto il contenuto della scheda a
        sinistra della larghezza del riquadro, con una regione che lo tagliava. Ma la barra di stato
        ("11 elementi | 1 elemento selezionato") è un elemento DirectUI (`StatusBarModuleInner`) in fondo a
        tutto il contenuto, dal bordo sinistro: il suo testo finiva sotto la colonna o fuori dalla
        finestra. Adesso resta dov'è, come quando si spegne il riquadro da Visualizza.
      - **Strade provate e scartate.** Nascondere soltanto la sink del riquadro non basta: DirectUI non
        allarga la vista. Senza adattare la regione, la parte allargata della vista resta tagliata;
    - vale solo per le finestre con la barra: l'impostazione "Riquadro di spostamento" di Esplora file
      non cambia;
    - lo spazio liberato va alla vista dei file e, se è acceso, al riquadro di anteprima, come quando il
      riquadro di spostamento si nasconde da Esplora file.
  - **Riquadro spento da Visualizza** (`WindowLayout::RefreshNavPane`): Esplora file nasconde il
    `CtrlNotifySink` del riquadro (non lo distrugge), solo nella scheda attiva. Con il riquadro nascosto
    nella scheda in primo piano, la barra non occupa spazio: niente spostamento verso il basso, niente
    regione, barra nascosta. La scheda appena creata non ha ancora il riquadro (arriva dopo 30-90 ms, già
    acceso o spento): intanto vale l'impostazione salvata da Esplora file per le schede nuove, il secondo
    DWORD di `HKCU\…\Explorer\Modules\GlobalSettings\Sizer\PageSpaceControlSizer` (1 = acceso). Si
    ricontrolla:
    - subito, con un `SetWinEventHook` fuori contesto sul thread della finestra (solo comparsa e
      scomparsa di `CtrlNotifySink`, `NamespaceTreeControl`, `ShellTabWindowClass` e `SHELLDLL_DefView`, cioè
      la vista ricreata cambiando cartella, che va allargata). La callback manda solo un messaggio: il layout
      si rifà al giro dopo, prima del disegno;
    - al cambio di scheda: la `ShellTabWindowClass` della nuova passa in cima (`WM_WINDOWPOSCHANGING`
      senza `SWP_NOZORDER`, nel subclass che c'è già);
    - nel controllo periodico (ogni 500 ms), come rete di sicurezza.

    Il log della mod dice da dove è arrivato il cambio (`subito`, `controllo periodico`, `barra accesa`).
- **Impostazioni**: `[VerticalTabs]` in `settings.ini` (`Enabled`, `Width`, `Collapsed`, `Suspended`) e `[NavBar] Enabled`,
  rilette dalla mod quando il file cambia (notifiche di modifica della cartella: a riposo nessun
  controllo). Costo a riposo misurato: circa 1 ms al secondo sul thread di una finestra.
- **Colori** (`TabColors.cpp`, `VerticalTabs.cpp`; `TabColorFiles.cpp` nella tray): il colore vive nel
  pannello, legato al modello della scheda nativa. Il sottomenu "Colore scheda" si aggiunge al menu
  originale della scheda mentre si apre e si toglie quando si chiude. Mod e tray si scambiano file
  in `%LOCALAPPDATA%\VTabsExplorer\colors` (scrittura atomica):
  - `live-<hwnd>.txt`, scritto dalla mod: numero di schede e, per ogni scheda colorata, posizione,
    colore e titolo;
  - `pending-<hwnd>.txt`, scritto dalla tray: i colori da rimettere alle schede appena riaperte. La mod
    li applica quando la finestra ha quel numero di schede (o dopo 20 s) e cancella il file.

  La tray segue ogni colore con la finestra `ShellTabWindowClass` della scheda, così il colore resta
  giusto anche quando la scheda si sposta. Un colore pubblicato vale solo se nella sua posizione c'è una
  scheda con quel titolo: la tray può avere ancora l'ordine di prima di un riordino. Quando i colori
  cambiano, la tray rilegge l'ordine dalla barra invece di usare quello tenuto da parte, e se i titoli
  non tornano riprova fino a 3 volte. Nella sessione ogni scheda colorata ha una riga `color <chiave>`
  (chiavi come VTabsNotepad).

**Sicurezza della mod**:
- ogni gestore di eventi verso il runtime XAML è protetto: nessuna eccezione esce dal nostro codice;
- la tray sorveglia ogni processo con la mod: se termina con un codice d'errore (`0xC...`: eccezione
  non gestita, fail fast) la funzione viene **sospesa** subito (`Enabled=0`, `Suspended=1`), un fumetto
  lo dice e non si inietta più finché non la si riattiva dal menu. Una chiusura normale o forzata da uno
  strumento (`0xFFFFFFFF`) non conta;
- la mod non si può scaricare: spegnendo la voce torna la barra orizzontale, ma la DLL resta caricata
  fino al riavvio di Esplora file; una versione nuova entra solo dopo un riavvio di Esplora file;
- nella cartella dei dati: mentre esiste `dev-windows.txt` (HWND esadecimali) le impostazioni valgono
  solo per le finestre elencate e tutte le altre, cioè quelle dell'utente, restano esattamente come sono
  (i test lo creano prima di cambiare qualsiasi impostazione e lo tolgono dopo averle rimesse);
  `mod-command.txt` e `dump-tree.txt` accettano comandi di prova rivolti solo alla finestra indicata
  (HWND in testa), mai alle altre.

## Compilazione e uso

Requisiti: Visual Studio con strumenti C++ x64 (CMake e Ninja inclusi), Windows SDK.

```powershell
.\build.ps1                       # Release in build\bin (chiude e riavvia la tray se è aperta)
build\bin\VTabsExplorer.exe          # avvia la tray
build\bin\VTabsExplorer.exe --restore-now   # riapre subito l'ultima sessione
build\bin\VTabsExplorer.exe --reopen-tab    # riapre l'ultima scheda chiusa (come Ctrl+Maiusc+T)
build\bin\VTabsExplorer.exe --exit   # chiude la tray
build\bin\VTabsExplorer.exe --inject-pid N  # (sviluppo) mod in un explorer.exe di prova, mai in quello
                                         # della barra delle applicazioni
```

`build.ps1` genera prima le proiezioni C++/WinRT della mod (`tools\Generate-Projections.ps1`) dai
`.winmd` del runtime WinUI 3 che usa Esplora file stesso (`%windir%\SystemApps\Microsoft.WindowsAppRuntime.CBS*`,
quello caricato da explorer.exe), più i metadati di WebView2 dal pacchetto NuGet ufficiale; si
rigenerano da sole quando cambia la versione del runtime. Una mod ricompilata entra in Esplora file
solo dopo il suo riavvio (la tray la porta nel processo nuovo alla prima finestra).

## Test

`tools\test\Run-Tests.ps1` mette la tray in **modalità prova**: `dev-filter.txt` nella cartella dei
dati indica `%TEMP%\xpx-test`, e VTabsExplorer considera solo le finestre con schede lì dentro (usando
`session-dev.txt`); il velo si tende solo mentre esiste `dev-arm.txt`, che la prova crea subito prima
di aprire la finestra che fa scattare il ripristino. Le finestre dell'utente non vengono toccate né
lette (si controlla solo che il loro numero non cambi). Il test apre finestre e schede di prova con le
API della shell (nessun input reale, ma le finestre si aprono in primo piano) e verifica: sessione
salvata (ordine delle schede, scheda attiva, posizione), chiusura insieme e una alla volta, riapertura
dalla cartella iniziale e da una cartella precisa, cartelle virtuali e cartelle sparite, opzione spenta,
"Riapri ora", riavvio della tray, finestra ingrandita, molte schede con la barra che scorre (ordine
salvato e ripristinato, scheda attiva fuori vista), chiusura al primo comando, file della sessione
occupato da un altro programma (salvato appena si libera), comando "nuova scheda" perso da Esplora
file (con `dev-drop-newtab.txt`, attivo solo in modalità prova: con un comando perso la scheda si
chiede di nuovo e la finestra torna completa; con la stessa scheda persa due volte la finestra torna
senza e Ctrl+Maiusc+T la riporta). A ogni ripristino un
osservatore controlla ogni 5 ms le finestre nuove (visibile, velata, numero di schede): devono restare
velate finché si costruiscono e comparire già con tutte le schede; con l'opzione spenta o con altre
finestre aperte la finestra nuova non deve essere velata; alla fine nessuna finestra deve restare
invisibile. Il pulsante "Torna su" si prova su una cartella con 300 file: assente in cima, compare
scorrendo, nell'angolo della vista, il clic (messaggi del mouse alla sua finestra) riporta all'inizio
senza togliere il primo piano a Esplora file, sparisce con la finestra ridotta a icona e torna, si
spegne e si riaccende dalle impostazioni. "Riapri la scheda chiusa" si prova senza premere tasti
davvero: che la combinazione sia riservata si verifica provando a registrarla dal test (mentre la tiene
la tray non ci riesce) e l'azione si chiede con `--reopen-tab`, lo stesso comando della scorciatoia;
si controlla che la combinazione sia nostra solo con una finestra di prova in primo piano e non ridotta
a icona, che una scheda chiusa torni in fondo alla barra e attiva, che due schede tornino a ritroso,
che una finestra chiusa torni intera (schede, scheda attiva, posizione) e già completa senza far
scattare il ripristino della sessione, che una scheda la cui finestra non esiste più torni in quella in
uso, che a pila vuota non succeda niente e che l'opzione la spenga e la riaccenda. Questi controlli, e quello sul primo piano dopo un ripristino,
richiedono una sessione con una finestra in primo piano: con il client di Desktop remoto ridotto a icona
Windows non attiva finestre e vengono saltati con un avviso, a meno che sul PC client il valore DWORD
`RemoteDesktop_SuppressWhenMinimized` = 2 in `HKCU\Software\Microsoft\Terminal Server Client` (client
riavviato) tenga attiva la sessione anche ridotta a icona. Alla fine chiude le finestre di prova
(l'ultima con la posizione di una finestra dell'utente: Esplora file apre le finestre nuove come
l'ultima chiusa), toglie la modalità prova e rimette le impostazioni. Con le schede verticali attive la
suite gira con il pannello su tutte le finestre di prova (lo stesso codice di tutti i giorni).

`tools\test\Test-OpenInTabs.ps1` prova le cartelle aperte da altri programmi, in modalità prova e con
`dev-merge.txt` nella cartella dei dati: senza, in modalità prova la funzione è spenta (le altre prove aprono
finestre che devono restare finestre); con "external" dentro, ogni finestra nuova conta come aperta da un altro
programma (la prova non decide chi è in primo piano). Apre le cartelle come i programmi veri:
`explorer.exe <cartella>` e `explorer.exe /select,<file>` (riga di comando, es. IntelliJ: Windows apre la
finestra in un explorer.exe "/factory" a sé, non in quello della barra), ShellExecute sulla cartella (come
OBS: finestra nel processo della barra) e SHOpenFolderAndSelectItems (come "Mostra nella cartella" di molti
programmi). Controlla: scheda nuova, attiva, con il file selezionato quando c'è un file da mostrare; nessuna
finestra in più né velata; la cartella iniziale resta una finestra; con più finestre la scheda va in quella
aperta per ultima e, portata in primo piano l'altra, in quella usata per ultima; con la funzione spenta resta
una finestra; senza "external" e con una finestra di prova in primo piano (come Ctrl+N) resta una finestra.
Per portare in primo piano una finestra di prova usa AttachThreadInput, solo se in primo piano c'è già una
finestra di prova. Risultato (2026-11-05): **19 su 19**; suite principale 64 su 64.

Le schede verticali hanno prove a parte in `tools\test\vertical-tabs\`, tutte su finestre di prova in un
**explorer.exe separato** (`explorer.exe /separate,<cartella>`): la tray in modalità prova con
`[VerticalTabs] SkipShellProcess=1` (scritto prima di `Enabled=1`) non entra mai nel processo della barra
delle applicazioni; `dev-windows.txt` (`Enter-VtIsolation` del TestKit) tiene ferme le finestre dell'utente per tutta la prova, anche quando la mod è già nel processo principale (verificato campionando ogni 300 ms la finestra dell'utente: nessun cambiamento); alla fine il processo di prova viene chiuso e le impostazioni rimesse:
- `Test-Basics.ps1`: pannello, contenuto a destra, colonna in cima alla finestra con l'intestazione dal suo
  bordo, riga del titolo nascosta (isola salita, contenuto subito sotto, barra degli indirizzi in cima e
  allineata ai pulsanti della colonna, casella di ricerca prima dei pulsanti della finestra), zona di
  trascinamento stretta sui pulsanti della finestra più il tratto libero, fascia del titolo (sotto il suo
  spazio vuoto c'è la finestra, che risponde `HTCAPTION`; i pulsanti restano del pannello: `WindowFromPoint`,
  che rispetta le regioni come il mouse vero), bordo in cima che ridimensiona, righe e scheda attiva come
  quelle native, selezione, "+", "X", ridimensionamento, spegnimento (intestazione di nuovo a tutta larghezza)
  e riaccensione; niente ricerca nel pannello; pulsanti della finestra nostri (alti quanto la barra degli
  indirizzi, quelli di DWM nascosti, primi nell'ordine Z, riduci/ingrandisci/chiudi su tutta la riga, chiudi rosso
  con il comando di prova `caption hover 2`, di nuovo quelli di DWM spegnendo) (36 controlli). Chi riceve il mouse
  in un punto si guarda solo tra le figlie della finestra di prova (`Get-ChildAt` del TestKit: ordine Z, visibilità,
  rettangolo e regione): con `WindowFromPoint` una finestra dell'utente in primo piano sopra quella di prova
  falsava i controlli (da un processo in secondo piano non la si porta sopra);
- `Test-Panel.ps1`: stile dei titoli, comprimi/espandi, niente ricerca nel pannello, riordino, menu contestuale
  nativo (sotto il punto del clic, nella colonna), larghezza (25 controlli; riordino, menu e larghezza con i
  comandi di prova di `mod-command.txt`, rivolti alla sola finestra di prova);
- `Test-Guard.ps1`: iniezione automatica della tray, crash simulato (processo di prova terminato con
  `0xC0000005`) che sospende la funzione, riattivazione dal menu, chiusura forzata che non conta (13);
- `Test-Mouse.ps1`: **mouse e tastiera veri** (si interrompe se il mouse viene mosso da fuori):
  passaggio del mouse, clic con il focus che torna alla vista dei file, clic centrale, trascinamento
  delle righe, clic destro, sottomenu "Colore scheda", divisore e doppio clic, comprimi/espandi, barra
  del titolo (trascinata dalla fascia in cima alla colonna e dal tratto libero a sinistra dei pulsanti della
  finestra; doppio clic sulla fascia che ingrandisce e ripristina, con la colonna sempre in cima; clic destro
  con il menu di sistema), Ctrl+Tab, F6 (31 controlli, tolti quelli della ricerca). Prima di ogni
  pressione il punto deve stare su una finestra del processo di prova e i tasti partono solo con lei in primo
  piano;
- `Test-Colors.ps1`: colori delle schede: voce nel menu nativo, aspetto della riga, file pubblicati
  (con i titoli), sessione della tray sulla scheda giusta, colori che seguono il riordino, colori rimessi
  (`pending`), chiusura; con `-Restore` anche "Riapri ora", che riapre nel processo **principale**, e
  con `-ExpectRestoredColors` la finestra riaperta deve avere ordine e colori giusti (serve la mod con i
  colori nel processo principale). Il sottomenu non si può aprire con UI Automation (`Expand` sulla voce
  chiude tutto il menu, col mouse vero funziona): qui i colori si scelgono con il comando di prova
  `color` di `mod-command.txt`, il sottomenu vero lo prova `Test-Mouse.ps1`;
- `Test-ForcedClose.ps1`: Esplora file di prova terminato a forza tre volte, la finestra deve tornare
  nella sessione con tutte le schede;
- `Test-Header.ps1`: barre del colore della barra del titolo, più la zona vetro (19 controlli). Per la zona
  vetro, il margine sinistro del contenuto salito deve avere lo stesso colore in cima e più in basso, anche da
  ingrandita e dopo il ripristino; `PrintWindow` vede la fascia (`#393939` con la zona di Esplora file). Spente le
  schede verticali, il log della mod deve dire che è tornata la zona di Esplora file. Si confrontano i colori
  campione dello screenshot e gli sfondi letti dall'albero visuale (`dump-tree.txt`, che ora riporta
  sfondi, bordi e angoli). Con la riga del titolo nascosta il colore della barra del titolo si prende dal
  tratto libero a sinistra dei pulsanti della finestra e dalla fascia in cima alla colonna. Casi provati:
  - schede verticali spente e riaccese: spente tornano esattamente i valori originali;
  - cambio di tema con le schede verticali accese, poi spente: tornano i valori del tema nuovo. Il
    tema si cambia solo per l'intestazione della finestra di prova, con il comando di prova `theme`
    (vale anche con la colonna spenta);
  - cambio di scheda, "Questo PC", menu "Altre opzioni" aperto (e compare sotto il suo pulsante,
    nell'intestazione spostata a destra della colonna).
- `Test-NavBar.ps1`: riquadro di spostamento in alto (46 controlli). Le voci sono le
  cartelle dell'utente: si confrontano con quelle del riquadro nativo (letto dall'albero del processo di
  prova) solo nel codice, i nomi non si stampano mai. Controlli:
  - geometria: barra sotto l'intestazione e larga quanto lei, alta 34 DIP; colonna in cima alla finestra,
    intestazione e barra dal suo bordo; vista dei file al bordo della colonna; riquadro nativo fuori vista
    a sinistra del contenuto, che non ha regione;
  - barra di stato: il suo primo testo ("N elementi", UI Automation) comincia dal bordo della colonna, con
    la barra accesa, riaccesa e senza schede verticali;
  - voci della barra più quelle dell'elenco "»" uguali al riquadro nativo, tutte e in ordine, comprese le
    cartelle con il nome invisibile;
  - divisori nelle stesse posizioni del riquadro di sistema. Il riquadro ha la riga doppia per le voci
    con la linea sopra (`TVM_GETITEMRECT`); quelli della barra si leggono da `navbar info`;
  - clic su "Galleria" (UI Automation, come il mouse), "Questo PC" dall'elenco, "Home" in una scheda nuova
    (comandi di prova `navbar open/newtab`);
  - menu classico della voce (`navbar classic`, voci lette dal menu, chiuso con Esc);
  - clic destro (`navbar context`) su una cartella aggiunta: il menu nuovo con le sue voci (UI
    Automation), confrontato con quello del riquadro di sistema per la stessa cartella. Il riquadro è
    nascosto dalla barra ma vivo: gli si manda `WM_CONTEXTMENU` sul punto della voce, nella scheda nuova,
    il cui riquadro ha già le cartelle di prova. Le differenze ammesse:
    - le voci dell'albero (Rinomina, Espandi/Comprimi);
    - "Apri con" di un'estensione installata, che a volte non si carica nemmeno fuori da Esplora file;
  - nel menu nuovo, "Apri in una nuova scheda" scelta con UI Automation;
  - "Mostra altre opzioni", che apre il menu classico (con Nuovo, senza Crea collegamento);
  - nel menu classico, "Nuovo" > "Cartella": sottomenu aperto con i messaggi del menu
    (`MN_SELECTITEM`, `MN_OPENHIERARCHY`), Invio mandato alla sua finestra, cartella creata;
  - riordino. Accesso rapido è uno solo per l'utente, quindi si usano 3 cartelle di prova
    (`xpx-pin-1..3`): aggiunte in fondo e tolte alla fine. Le finestre dell'utente le vedono per la
    durata della prova. L'elenco delle fissate si legge all'inizio e alla fine deve essere identico
    (`XpxPin.PinKit`, con la stessa interfaccia della mod). Si verificano:
    - gli spostamenti con il comando di prova `navbar move` (lo stesso codice del rilascio);
    - lo stesso ordine nella barra e in Windows (indici delle fissate);
    - la riuscita di `PinItem`;
    - la Home aperta dopo, che mostra lo stesso ordine in Accesso rapido;
  - con `-Mouse` (mouse vero nella sola finestra di prova):
    - trascinamento nella barra;
    - un trascinamento non apre la cartella, un clic semplice sì;
    - Esc annulla;
    - dall'elenco "»" alla barra, e l'elenco si chiude;
    - dalla barra all'elenco, fermandosi su "»".

    Una guardia controlla, prima di ogni movimento, pressione o Esc, che in primo piano ci sia la
    finestra di prova (o un suo popup) e che sotto il punto non ci sia altro. Altrimenti rilascia il
    tasto e salta le prove col mouse rimaste;
  - "Riquadro di spostamento" spento e riacceso dal menu Visualizza > Mostra della finestra di prova (UI
    Automation, nessun input reale):
    - la barra sparisce e ricompare entro poche decine di ms, con il log `(subito)`;
    - con il riquadro spento, la vista dei file sta sotto l'intestazione (la colonna resta in cima),
      senza regione. Per questa parte la finestra di prova è più larga (1500 px): con la colonna in cima
      l'intestazione è più stretta e "Visualizza" finirebbe nel menu "Altre opzioni";
    - passando a una scheda col riquadro acceso la barra torna, e tornando indietro sparisce;
    - una scheda nuova (riquadro spento anche lì) non fa comparire la barra;
    - alla fine si rimette com'era l'impostazione salvata da Esplora file (`PageSpaceControlSizer`), che
      cambiando il riquadro diventa quella delle schede nuove;
  - finestra stretta e di nuovo larga; barra spenta e riaccesa; schede verticali spente.
  - `-DefineOnly` (con il punto) carica solo le funzioni, per le prove a mano.
- `Test-NavGroups.ps1`: gruppi della barra (62 controlli, 15 in più con `-Mouse`; `-MouseOnly` fa solo la
  preparazione e i passi col mouse vero, circa 2-3 minuti, per le diagnosi), nella stessa finestra di prova
  isolata. Usa 4
  cartelle di prova (`xpx-pin-1..4`), aggiunte ad Accesso rapido e tolte alla fine (le fissate dell'utente
  devono tornare identiche), e il file dei gruppi di prova `navbar-groups-test.json`: il processo di prova
  non è quello della barra delle applicazioni. La prova controlla che il file dell'utente non cambi.
  Controlli:
  - gruppo nuovo con cartelle dentro, salvato subito nel file; spostato in testa al blocco è un pulsante
    della barra (`AutomationId` `NavGroup`);
  - clic sul gruppo (UI Automation): l'elenco con le cartelle in ordine; clic su una cartella: si apre e
    l'elenco si chiude;
  - sottogruppo: una riga a sé (`NavGroupRow`), il cui elenco si apre a lato con quello del gruppo ancora
    aperto; "(vuoto)" per un gruppo vuoto;
  - con un elenco aperto, il cursore di prova (`navbar group hover`) su un altro gruppo: il suo elenco si
    apre entro 60 ms dal comando, senza animazioni, ed è evidenziato solo il suo pulsante; poi su "»" e da
    "»" di nuovo a un gruppo; chiuso l'elenco nessun pulsante evidenziato, e riaperto di nuovo con le
    animazioni. Per questo blocco il gruppo vuoto va in testa, così a scale alte (225%) entrambi i gruppi
    sono nella barra e non in "»", e alla fine torna al suo posto;
  - aperto col clic (UI Automation), cambiato passandoci sopra e chiuso: il clic sul primo gruppo ne riapre
    l'elenco;
  - trascinamento guidato dai comandi di prova (`drag`, `dragto` con i punti di UI Automation portati in
    pixel fisici, `dragup`): dall'elenco di un gruppo, fermi su un altro gruppo della barra, si apre il suo
    elenco al posto di quello di partenza e il trascinamento continua staccato; la cartella si lascia sopra la
    prima riga del nuovo elenco, che resta aperto e aggiornato, e i suggerimenti dei due gruppi dicono il
    contenuto nuovo (dal registro: UI Automation non espone i suggerimenti). Poi lo stesso dall'elenco di un
    sottogruppo, lasciata in fondo. Alla fine gruppi e ordine tornano com'erano;
  - menu del gruppo con le sue quattro voci; "Apri tutte" e il clic centrale aprono le schede in ordine;
  - riquadro del nome: "Rinomina…" (testo scritto con `ValuePattern`, poi "Salva"), "Annulla", "Nuovo
    gruppo…" dal clic destro sullo sfondo della barra;
  - menu classico di una cartella: "Sposta nel gruppo" (sottomenu con i percorsi, scelto con i messaggi
    del menu) e "Togli dal gruppo", anche passando da "Mostra altre opzioni" del menu nuovo;
  - trascinamento con `navbar group drop`, lo stesso calcolo del puntatore vero: al centro di un gruppo ci
    entra, sul bordo va prima, riordino nell'elenco (che resta aperto), dentro un sottogruppo, dal gruppo
    alla barra. La linea e il gruppo illuminato si leggono dal registro; un gruppo non entra in un suo
    sottogruppo;
  - l'ordine di Windows delle cartelle di prova è sempre quello della barra letta in profondità (dopo ogni
    operazione), e il riquadro di sistema nascosto dalla barra è già nello stesso ordine;
  - "Elimina gruppo";
  - barra spenta (`[NavBar] Enabled=0`): il riquadro di sistema torna con le cartelle nell'ordine della barra;
  - "Rimuovi da Accesso rapido" dal menu della barra: la cartella esce anche dal gruppo;
  - un processo di prova nuovo, con la barra spenta e l'ordine di Windows cambiato da fuori mentre era chiuso
    (una cartella di un gruppo portata in cima con `PinItem`): la mod rimette l'ordine dei gruppi e il
    riquadro di sistema, visibile, lo mostra; riaccesa la barra, gli stessi gruppi allo stesso posto. Nessun
    errore della mod nel registro;
  - con `-Mouse`, mouse e tastiera veri nella sola finestra di prova (con la guardia del primo piano di
    `Test-NavBar.ps1`): clic su un gruppo; fermi sul sottogruppo si apre, fermi su un'altra riga si chiude;
    clic su una cartella dell'elenco; cartella trascinata al centro di un gruppo (ci entra, e non si apre);
    fermi su un gruppo trascinando l'elenco si apre e la cartella si lascia in cima; dall'elenco alla barra;
    un gruppo dentro un altro; Esc annulla; clic destro sul gruppo; il nome scritto con la tastiera
    (Backspace e Invio restano nella casella); con un elenco aperto, passando sopra un altro gruppo si apre
    il suo; trascinando dall'elenco di un gruppo e fermandosi su un altro gruppo della barra si apre il suo, e
    la cartella si lascia in cima al nuovo elenco.
  - `-DefineOnly` (con il punto) carica solo le funzioni, per le sonde.

  Le altre prove delle schede verticali spengono la barra (`[NavBar] Enabled=0`), così la loro geometria
  resta quella che controllano.

## Stato

Verificato dal vivo su Windows 11 25H2 (build 26200) con la suite di test: 30 controlli su 30 (compresi
i casi con molte schede e barra che scorre); con il pulsante "Torna su" (2026-10-29) 40 su 40 e con
"riapri la scheda chiusa" (2026-11-02, Windows 26H2 build 26300) **53 su 53**, con il client di Desktop
remoto ridotto a icona e `RemoteDesktop_SuppressWhenMinimized` = 2 (senza, i controlli che richiedono
una finestra in primo piano si saltano); con il caso della scheda la cui finestra non c'è più e con
quello della sessione ripristinata (vedi sotto), **55 su 55**. La pressione vera di Ctrl+Maiusc+T è stata provata a parte (la suite non premerebbe tasti): con
la finestra di prova in primo piano la scheda chiusa è tornata.

Dopo una revisione del codice (2026-11-02) sono state corrette, nella funzione nuova: le pressioni
perse quando si premeva più volte di fila (ora fanno coda: riaprire può durare qualche secondo); la
riapertura in una finestra non considerata (in modalità prova poteva finire in una finestra
dell'utente); la pila non svuotata passando da modalità prova a normale; la scheda chiusa persa quando
non c'era nessuna finestra in cui riaprirla (adesso resta in pila); il conteggio delle schede chiesto a
Esplora file invece che letto localmente; l'ordine fra più schede chiuse nello stesso mezzo secondo
(ora per ora di chiusura, dal gancio sulle distruzioni); una finestra riaperta che poteva far scattare
anche il ripristino della sessione. In una seconda verifica è emerso un caso che nessuno aveva provato:
chiusa l'ultima finestra e riaperto Esplora file, la sessione tornava da sola, ma la finestra restava
anche nella pila di Ctrl+Maiusc+T, che la riapriva **una seconda volta**. Ora le finestre tornate con il
ripristino (anche con "Riapri ora") escono dalla pila; c'è un controllo apposta nella suite.

Costo a riposo misurato con la funzione attiva (una finestra, due schede): il controllo della
scorciatoia non sveglia il suo thread quando si è in un altro programma (zero risvegli in 20 s), e il
consumo resta quello di prima — qualche decina di millisecondi ogni 30 s, con punte di 60-80 ms quando
cade la rilettura completa. Tempo in cui la finestra resta velata (dalla creazione alla
comparsa, già completa): 0,9 s con 2 schede, 1,7 s con 4-5 schede, 4,2 s con 11 schede; con due
finestre (3 + 2 schede) 2,8 s, perché compaiono insieme e Esplora file, occupato con la prima, impiega
circa 2 s ad aprire la seconda. Per ogni scheda in più Esplora file lavora circa 0,3 s sul thread della
finestra (crea la scheda, carica la cartella iniziale, poi quella voluta): è il limite, nascosta o no
la finestra non va più veloce. CPU della tray a riposo invariata (circa 30 ms ogni 30 s).

Note tecniche emerse:
- il comando "nuova scheda" va mandato solo quando la scheda creata prima è in cima (attiva) e ha la
  sua vista: mandandone più di uno di seguito le schede in più restano scollegate dalla loro
  intestazione nella barra (selezionarle non le mostra, i titoli non si aggiornano) e la finestra non
  si chiude più con la X (chiude solo le schede collegate). Una finestra così si sblocca aprendo una
  scheda nuova e poi chiudendola con il comando di Ctrl+W (0xA021): si chiude tutta;
- Esplora file (Windows 11 25H2) può creare in anticipo una finestra nascosta ("prelaunch", vedi
  `HKCU\...\Explorer\Prelaunch`) e mostrare quella quando lo si apre: per questo il velo guarda anche la
  comparsa e non solo la creazione;
- da un altro processo DWM non permette di "mascherare" una finestra (`DWMWA_CLOAK`: accesso negato),
  mentre `WS_EX_LAYERED` con opacità zero funziona e togliendolo la finestra torna identica;
- il verbo `opennewtab` invocato da fuori apre una finestra nuova, non una scheda;
- chiudendo una finestra con più schede, Esplora file ha mostrato una volta per meno di un secondo
  un'altra finestra con parte delle schede: una finestra comparsa quando le altre avevano già
  cominciato a chiudersi e sparita entro 2 s non entra nella sessione. L'ora di chiusura di ogni
  finestra viene dall'evento di distruzione (al millisecondo), così l'ordine "ultima chiusa per prima"
  resta giusto anche quando un controllo ne trova chiuse più d'una;
- con molte schede la barra scorre ed è virtualizzata: le schede fuori vista possono mancare del tutto in
  UI Automation, e l'ordine dell'albero può risultare "ruotato" dal riciclo degli elementi. Per questo
  ogni scheda presente va al posto indicato da `PositionInSet` e quelle mancanti all'ultima posizione nota;
  una scheda attiva fuori vista si fa comparire (`ItemContainerPattern` + `VirtualizedItemPattern`) prima
  di selezionarla;
- il comando "nuova scheda" (0xA21B) apre la scheda **in fondo** alla barra, anche con un'altra scheda
  attiva (verificato il 2026-11-02): creandole una dopo l'altra l'ordine è quello di creazione, ma una
  scheda riaperta non si può rimettere al suo posto in mezzo (Esplora file non espone un comando per
  spostare una scheda);
- Esplora file non ha una sua "riapri scheda chiusa": nella tabella degli acceleratori di
  `ExplorerFrame.dll` ci sono Ctrl+T (nuova scheda, 0xA21B), Ctrl+W (chiudi scheda, 0xA021) e
  Ctrl+Tab / Ctrl+Maiusc+Tab, e Ctrl+Maiusc+T è libera. Chiudendo la scheda attiva diventa attiva quella
  a **sinistra**, e la finestra `ShellTabWindowClass` della scheda resta la stessa anche cambiando
  cartella: è un'identità affidabile per accorgersi di quali schede vengono chiuse;
- con le schede, `WM_CLOSE` alla finestra chiude solo la scheda attiva: la finestra intera si chiude con
  `WM_SYSCOMMAND`/`SC_CLOSE` (come la X);
- aprire una cartella "e basta" potrebbe aggiungerla come scheda a una finestra già aperta: per una
  finestra nuova serve il verbo `opennewwindow`;
- ogni chiamata COM verso Esplora file passa dal thread della finestra, lento mentre crea una scheda:
  per ogni elemento di `IShellWindows` si chiede subito la finestra di appartenenza e i dettagli solo per
  quella che interessa;
- il processo di Esplora file è anche quello di barra delle applicazioni e desktop: ascoltarne gli eventi
  WinEvent costava più CPU del controllo locale a intervalli;
- ripristinando da icona una finestra di Esplora file mentre è in primo piano un'altra finestra (nei test:
  `ShowWindow(SW_RESTORE)` da un altro processo), Windows può darle il primo piano senza nessun
  `EVENT_SYSTEM_FOREGROUND`: arriva solo `EVENT_SYSTEM_MINIMIZEEND`, quando la finestra risulta ancora
  ridotta. Per questo dopo quell'evento il pulsante ricontrolla il primo piano per 3 s. Esplora file
  segnala inoltre il primo piano anche sulla scheda attiva (`ShellTabWindowClass`, una finestra figlia):
  vale la finestra principale.

**Schede verticali** (2026-11-02, Windows 26H2 build 26300, runtime WinUI 3 di Esplora file
`Microsoft.UI.Xaml` 3.2): sviluppate e verificate prima in un explorer.exe separato (19 + 26 + 13
controlli automatici, 23 con mouse e tastiera veri), poi attivate nell'explorer.exe principale, dove la
suite completa dà **55 su 55** con il pannello su tutte le finestre di prova. A riposo il pannello costa
circa 1 ms al secondo sul thread della finestra; la mod nessun controllo periodico sui file.

**Colore scheda** (2026-11-02): `Test-Colors.ps1 -Restore -ExpectRestoredColors` tutto superato. La
finestra riaperta nel processo principale torna con ordine e colori giusti, e il file da rimettere viene
consumato dalla mod. `Test-Mouse.ps1` ha dato 26 su 26 con il sottomenu vero, e `Test-Panel.ps1` 26 su 26.
Durante il riavvio di Esplora file che ha caricato la mod con i colori, una finestra dell'utente è
tornata **senza la sua ultima scheda**: Esplora file, appena ripartito, non aveva eseguito il comando
"nuova scheda" e la versione di allora rinunciava dopo 5 s. Da qui il nuovo tentativo, le schede
mancanti tenute per Ctrl+Maiusc+T con l'avviso, e il caso di prova apposta. Con questi correttivi, e
con quello del salvataggio della sessione su file occupato (trovato da un controllo fallito una
volta, "Sessione non salvata (errore 5)"), la suite principale dà **64 su 64**. A riposo con una
finestra e due schede: explorer.exe circa 5,5 ms di CPU al secondo (barra delle applicazioni compresa),
tray circa 1,6 ms al secondo.

**Intestazione del colore della barra del titolo** (2026-11-02, stesse build): `Test-Header.ps1` 17 su 17,
regressioni delle schede verticali (Basics, Panel, Guard, Colors) senza errori. Dopo il riavvio di Esplora
file con la mod nuova (12 s di attesa prima di "Riapri ora", nessuna scheda persa) la suite principale dà
**64 su 64**, e `Test-Colors.ps1 -Restore -ExpectRestoredColors` passa con la finestra riaperta nel
processo principale. Colori campione nel tema scuro: barra del titolo #202020, fascia degli indirizzi
#2C2C2C → #202020, fascia dei comandi #191919 → #202020. Costo misurato in un processo di prova con la
sola finestra di prova: uguale con la funzione accesa e spenta (le animazioni ferme non tengono
sveglio il disegno). Su richiesta la voce di menu e l'impostazione `UnifiedHeader` sono state tolte:
l'intestazione è sempre del colore della barra del titolo con le schede verticali accese, e torna
originale spegnendole. Con questa versione `Test-Header.ps1` dà 15 su 15 (acceso/spento con le schede
verticali), Panel 26 su 26, Basics 19 su 19, Guard senza errori.

**Riquadro di spostamento in alto** (2026-11-03): `Test-NavBar.ps1` 20 su 20. Le regressioni delle
schede verticali (Basics, Panel, Header, Guard, Colors) non danno errori. Con la barra attiva nel
processo principale, dopo il riavvio di Esplora file, la suite principale dà **64 su 64**. Nella
configurazione dell'utente: 39 voci, cioè 5 della shell e 34 aggiunte ad Accesso rapido, di cui 7 con
il nome invisibile. Su richiesta queste ultime non diventano più linee verticali: sono voci normali,
come nel riquadro nativo. Dopo il riavvio di Esplora file con questa versione, nella finestra
dell'utente ne stanno 17 e nella finestra di prova (1100 px) 11.

**Spostare le cartelle aggiunte** (2026-11-03): `Test-NavBar.ps1` 26 su 26 senza mouse. Col mouse vero
sono riuscite tutte e 7 le prove, in due giri: il secondo è stato lanciato con l'utente lontano dal PC,
perché nel primo la guardia aveva fermato le prove quando il mouse è stato mosso. Lo spostamento in
Windows (`PinItem`) dura 0-16 ms e la barra si rimette in pari con la lettura dopo, in circa 0,2 s.
Durante le prove la barra della finestra dell'utente si è aggiornata con al più 0,5 s di ritardo, senza
blocchi. Dopo ogni giro le fissate dell'utente (38: le 34 cartelle del riquadro, più 3 file e una
cartella virtuale che il riquadro non mostra, in fondo) sono identiche a prima.

**Divisori nella barra** (2026-11-03): `Test-NavBar.ps1` 27 su 27, con il controllo nuovo sui divisori.
Nella finestra di prova la linea fra Galleria e il Cestino è uguale ai separatori della barra dei
comandi; quella prima di Questo PC sta nell'elenco "»". Sono nel processo principale dal riavvio di
Esplora file delle 21:59, con la finestra dell'utente riaperta con le sue 3 schede. In quel momento però
la barra era spenta: l'aveva spenta l'utente dal menu della tray alle 21:50.

**La barra segue "Riquadro di spostamento"** (2026-11-03): `Test-NavBar.ps1` 37 su 37. Spento da
Visualizza, la barra sparisce in 26-36 ms; riacceso, o passando a una scheda col riquadro acceso, torna in
64-87 ms (dal comando di UI Automation alla finestra della barra). Basics 19 su 19 e Panel 26 su 26.
Header: 15 su 15 al secondo giro; al primo 2 errori sul riferimento del tema chiaro, perché l'albero è
stato letto prima che il tema cambiasse (codice non toccato da questa modifica). Le prove preliminari,
in una finestra senza mod, hanno mostrato come si comporta Esplora file:
- il riquadro si spegne solo nella scheda attiva, nascosto e non distrutto;
- le altre schede lo tengono anche quando tornano in primo piano;
- le schede nuove seguono l'ultima scelta;
- le finestre degli altri processi non cambiano.

Nel processo principale dal riavvio di Esplora file delle 22:55, con la finestra dell'utente riaperta
con le sue 3 schede e la barra accesa (riaccesa dall'utente alle 22:24).

**Menu nuovo di Windows 11 sul clic destro** (2026-11-04): `Test-NavBar.ps1` 43 su 43; Basics 19 su 19,
Panel 26 su 26, Header 15 su 15. Le voci sono le stesse del riquadro di sistema, salvo quelle dell'albero.
"Nuovo" c'è grazie all'unione con il menu "di sfondo". La convivenza con il presenter della vista dei file
(il caso reale) è stata provata alternando cinque volte i due menu, in tutti e due gli ordini: nessun
problema.

Durante le prove il processo di prova si è chiuso (0xC0000409 in ucrtbase, cioè `abort`). La causa è
stata trovata così:
- una diagnosi solo per le prove (`dev-exceptions.txt`, `DevDiagnostics.cpp`) ha registrato le eccezioni
  C++ con lo stack, poi tradotto con i simboli;
- il passo che chiudeva il processo era "Mostra altre opzioni" del menu del **riquadro di sistema**,
  scelto con UI Automation e seguito dalla lettura da fuori del menu classico. Lo fa anche **senza la
  mod**, quindi è un difetto di Windows provocato dal modo di provare: le prove non lo fanno più.

**Gruppi nella barra** (2026-11-04): `Test-NavGroups.ps1` 56 su 56 con `-Mouse` (42 su 42 senza),
`Test-NavBar.ps1` 49 su 50 con `-Mouse`, `Test-Panel.ps1` 26 su 26. La barra è stata in buona parte riscritta
(voci per chiave, elenchi generalizzati). L'unico controllo mancato di `Test-NavBar.ps1` è "riacceso da Visualizza":
la barra è tornata in 104 ms, ma l'ha rimessa il controllo periodico invece dell'evento di Windows, una corsa fra
i due (in quella funzione non è cambiato nulla; nei due giri prima era passato). Nel processo principale dal
riavvio di Esplora file delle 03:47, con la finestra dell'utente riaperta con le sue 5 schede e nessun gruppo
all'inizio. Durante le prove:
- "Apri tutte" ha fatto cadere il processo di prova: due schede nuove una sopra l'altra (vedi note
  tecniche). Ora le schede nuove si aprono una alla volta; ci passano anche i clic centrali, che con due
  clic veloci potevano provocare lo stesso crash anche nella versione di prima;
- le voci dei gruppi aggiunte al menu nuovo non comparivano: il presenter tiene solo i verbi di Windows
  (vedi sopra). Restano nel menu classico;
- col mouse e la tastiera veri (`-Mouse`, autorizzati dall'utente alle 02:50) sono emersi tre problemi,
  tutti corretti e riprovati:
  - aprire una cartella dalla barra riscriveva l'ordine vecchio delle fissate in Windows, quindi uno
    spostamento si perdeva al primo clic. Succedeva anche col riordino del 2026-11-03: ora si apre la
    cartella vera (vedi "Aperture");
  - Backspace in una nostra casella di testo era "Indietro" di Esplora file: la scheda tornava alla cartella
    di prima. Valeva anche per "Cerca nelle schede" delle schede verticali, dove anche Esc non arrivava. Ora
    c'è `TextInput`;
  - con un elenco aperto, passare sopra un altro gruppo della barra non apriva il suo: ora lo fa un timer
    che legge il puntatore.

  Dopo le prove col mouse, la finestra di un processo di prova nuovo (`explorer.exe /separate,<B>`) si è
  aperta più volte in una cartella del profilo (9 lettere) invece che su B, mai senza mouse: lo si vede
  solo nelle prove (la prova la trova dal processo), non nella mod.

**Ordine dei gruppi nel riquadro di sistema** (2026-11-04, richiesta dell'utente dopo aver provato i gruppi):
`Test-NavGroups.ps1` 60 su 60 con `-Mouse`, `Test-NavBar.ps1` 43 su 43. Nel processo principale dal riavvio di
Esplora file delle 04:35, con la barra spenta (scelta dell'utente): all'avvio la mod ha letto i suoi 4 gruppi, ha
spostato in Windows 20 cartelle nell'ordine dei gruppi (20 su 20 riuscite) e ha rimesso in ordine il riquadro di
sistema nelle 7 schede della finestra. Copia di sicurezza dell'ordine di prima (file del jump list e
`navbar-groups.json`) nella cartella temporanea della sessione.

Trovato nelle prove: `mod-command.txt` lo leggeva e lo cancellava ogni processo con la mod, compreso quello della
barra delle applicazioni, e il comando per la finestra di prova a volte si perdeva (un controllo mancato ogni
tanto). Ora lo prende solo il processo che ha la finestra indicata.

**Colonna in cima alla finestra, come Chrome** (2026-11-04, richiesta dell'utente: "tirare quanto più possibile
sù il pannello di tab verticali come fa chrome ... senza stravolgere troppo"): `Test-Basics.ps1` 25 su 25,
`Test-Panel.ps1` 27 su 27, `Test-Header.ps1` 16 su 16, `Test-Colors.ps1` 16 su 16, `Test-NavBar.ps1` 43 su 43,
`Test-NavGroups.ps1` 46 su 46, `Test-Guard.ps1` 12 su 12, `Test-ForcedClose.ps1` 3 su 3, `Test-Mouse.ps1` 34 su
34 con mouse e tastiera veri. Prima di toccare il codice, una prova a mano nella finestra di prova (isola
dell'intestazione spostata con `SetWindowPos` da fuori) ha mostrato che il suo XAML si adatta da solo alla
larghezza nuova. Trovato nelle prove:
- la prima versione lasciava passare il mouse nella fascia del titolo con `HTTRANSPARENT` dal subclass
  dell'isola: con `SendMessage(WM_NCHITTEST)` tutto tornava, ma col mouse vero la finestra non si trascinava
  e il doppio clic non la ingrandiva. Un registro dei messaggi (subclass dell'isola e della finestra, aggancio
  `WH_MOUSE` del thread) non ha visto nulla del clic vero: l'input delle isole XAML non passa da lì. Ora la
  fascia è fuori dalla regione dell'isola (vedi "Colonna in cima alla finestra"); un `WM_NCLBUTTONDBLCLK`
  mandato alla finestra con `HTCAPTION` la ingrandiva già, quindi la finestra principale era pronta;
- a 1100 px "Visualizza" finisce nel menu "Altre opzioni" (intestazione più stretta della colonna): la
  prova del riquadro di spostamento ora allarga la finestra di prova.

Con il mouse vero anche `Test-NavBar.ps1 -Mouse` 50 su 50 e `Test-NavGroups.ps1 -Mouse` 60 su 60. Nel processo
principale dal riavvio di Esplora file delle 06:32: nelle finestre dell'utente (ingrandite) colonna da y=3, intestazione
e barra delle cartelle dal suo bordo, fascia del titolo di 44 px. Il riavvio ha però riaperto **due finestre uguali**
(7 schede ciascuna) invece di una: prima del riavvio se ne vedeva una sola, ma nel processo vecchio ne esisteva una
seconda, probabilmente chiusa e tenuta nascosta da Esplora file per riusarla. La tray legge solo le finestre visibili
(`CabinetWindows`) ma tiene nella sessione quelle già note che esistono ancora nascoste: da correggere (difetto della
tray, non della colonna).

**Riga del titolo nascosta** (2026-11-04, richiesta dell'utente: "far salire anche la parte centrale ... dato che mettendo
le schede in verticale si libera spazio sulla barra del titolo"): `Test-Basics.ps1` 29 su 29, `Test-Panel.ps1` 27 su 27,
`Test-Header.ps1` 16 su 16, `Test-Colors.ps1` senza errori, `Test-Mouse.ps1` 34 su 34, `Test-NavGroups.ps1 -Mouse` 60 su
60, `Test-Guard.ps1` 12 su 12, `Test-ForcedClose.ps1` 3 su 3, `Test-NavBar.ps1 -Mouse` 49 su 50 (mancato solo il caso già
noto "riacceso da Visualizza": la barra torna in 98 ms ma con il controllo periodico invece che con l'evento). Nel
processo principale dal riavvio delle 07:21 (chiesto all'utente, che stava usando il PC): una finestra con le sue 5
schede, riaperta una volta sola (registro dettagliato della tray acceso per il riavvio: niente doppioni stavolta).

**Fascia chiara in cima al contenuto** (2026-11-04, segnalata dall'utente con le schede verticali e il riquadro di
spostamento nativo): era la zona vetro (vedi sopra), come nel Blocco note.
- Prove in un `explorer.exe /separate`, con la barra delle cartelle spenta: cima del contenuto `#191919` come più in
  basso, nel riquadro e nella vista, anche ingrandita, ripristinata e con il pannello compresso ed espanso. Con la
  barra accesa il limite segue la barra (165 px).
- Test: `Test-Header.ps1` 19 su 19, compresi i tre controlli nuovi; `Test-Basics.ps1` 29 su 29; `Test-Panel.ps1` 27 su 27.
- Nel processo principale dal riavvio delle 10:27 (annunciato): una finestra con le sue 5 schede.
- Nella finestra dell'utente, ingrandita, la cima del contenuto è `#191919` dal primo pixel. Letti pochi pixel dello
  schermo, nessuno screenshot: nota che PowerShell va reso DPI-aware, altrimenti le coordinate sono scalate del 125%.

**Barra di stato con il riquadro di spostamento in alto** (2026-11-05, segnalata dall'utente: con la barra accesa non si
vedeva "11 elementi | 1 elemento selezionato"). Il riquadro nativo ora si nasconde con le sink dentro il contenuto, non
spostando il contenuto (vedi "Spazio" sopra).
- Nella finestra di prova: contenuto dal bordo della colonna senza regione, sink del riquadro a -301..0, vista e
  `SHELLDLL_DefView` allargati a tutta la larghezza, "11 elementi" a x=307 (colonna fino a 292).
- Test: `Test-NavBar.ps1` 46 su 46 con i controlli nuovi sulla barra di stato, senza mouse; `Test-NavGroups.ps1` 46 su
  46; `Test-Panel.ps1` 27 su 27; `Test-Header.ps1` 19 su 19; `Test-Basics.ps1` 29 su 29. Al primo giro di Basics 5
  errori di "sotto il punto": la finestra di prova era finita sotto quella dell'utente, che stava usando Esplora file.
- Nel processo principale dal riavvio delle 12:49. Prima c'era una finestra di copia nascosta e l'utente ha confermato
  che non c'erano copie in corso. Dopo: 1 finestra, 5 schede. La barra delle cartelle dell'utente era spenta, per sua
  scelta: lasciata così.

**Ricerca nelle schede tolta** (2026-11-05, richiesta dell'utente, insieme a VTabsNotepad):
- Tolti la lente, la casella e il filtro dell'elenco, il blocco del riordino con il filtro attivo e l'uso di `TextInput`
  nel pannello. `TextInput` resta per la casella del nome dei gruppi della barra.
- Nella fascia del titolo resta solo comprimi/espandi. Non c'era un'impostazione da togliere: la ricerca era sempre
  accesa.
- Prove: `Test-Basics.ps1` 30 su 30, `Test-Panel.ps1` 25 su 25 (con "niente ricerca nel pannello"), `Test-Header.ps1`
  19 su 19. Da `Test-Mouse.ps1` è tolta la parte della lente, non rieseguito.
- Nel processo principale dal riavvio delle 13:13: 1 finestra, 4 schede (l'utente ne aveva chiusa una). L'utente ha
  riacceso la barra delle cartelle: lasciata accesa.

**Pulsanti della finestra alti quanto la barra degli indirizzi** (2026-11-05, richiesta dell'utente: "le barre del titolo
di explorer e notepad sono di grandezza più alta rispetto ai tre bottoni ... farli più grandi che coprano l'altezza della
riga? come fanno chrome e windows terminal"; insieme a VTabsNotepad). Vedi "Come funziona" (`CaptionButtons.cpp`).
- Finestra di prova al 125%: pulsanti da 58 px, alti 63 px (54 da ingrandita, dalla cima dello schermo); hit test
  riduci/ingrandisci/chiudi da y=1 a 62, `HTTOP` in cima; glifi identici a quelli di DWM (stessa somma dei pixel per
  riduci, 12 px). Spegnendo le schede verticali tornano quelli di DWM (`DWMWA_CAPTION_BUTTON_BOUNDS` di nuovo largo).
- Mouse vero sulla finestra di prova (`caption-mouse-xpx.ps1` nello scratchpad della sessione): chiudi rosso al
  passaggio, suggerimento "Chiudi", ingrandisci e ripristina, premuto su chiudi e trascinato fuori (la finestra resta),
  clic destro = menu di sistema, riduci a icona, chiudi; anche a finestra non attiva. I layout di ancoraggio non si sono
  potuti vedere: sono spenti nelle impostazioni dell'utente (`EnableSnapAssistFlyout=0`), lasciati così.
- Prove: `Test-Basics.ps1` 36 su 36, `Test-Header.ps1` 19 su 19, `Test-Panel.ps1` 25 su 25, `Test-NavBar.ps1` 46 su 46,
  `Test-Mouse.ps1` 31 su 31 (mouse vero).
- Nel processo principale dal riavvio delle 14:42 (annunciato; nessuna copia in corso): 1 finestra, 4 schede, pulsanti
  nostri attivi.

Tempi delle letture (in un thread a sé):
- la prima, con le icone: circa 190 ms;
- quelle senza novità: 30-50 ms.

Note tecniche emerse con le schede verticali:
- l'intestazione di Esplora file è **una sola isola XAML** (schede, barra degli indirizzi, ricerca e
  comandi): la striscia schede non si può spostare fuori vista come in VTabsNotepad, si rende invisibile
  dentro l'isola; il contenuto di ogni scheda è una `ShellTabWindowClass` a tutta larghezza, e tutte le
  finestre di una finestra di Esplora file (isole, schede, barra del titolo) stanno sullo stesso thread;
- la barra del titolo si trascina grazie a `TITLE_BAR_SCAFFOLDING_WINDOW_CLASS`, una finestra sopra la
  zona libera a destra delle schede che risponde `HTCAPTION` (le regioni di
  `InputNonClientPointerSource` sono vuote): allargata, tutta la riga del titolo si trascina. Risponde lei
  anche per i bordi (`HTTOP`, `HTTOPLEFT`, `HTLEFT` vicino al bordo) e per riduci/ingrandisci/chiudi
  (`HTMINBUTTON`, `HTMAXBUTTON`, `HTCLOSE`); un `WM_NCHITTEST` mandato da fuori sopra un pulsante lo lascia
  evidenziato finché il mouse non si muove (nelle prove, dopo, se ne manda uno su un punto vuoto). Alta 43 px
  al 125% dalla cima della finestra; l'isola dell'intestazione comincia 3 px sotto;
- la riga del titolo dell'isola è alta 38,4 DIP, la barra degli indirizzi 48, quella dei comandi 47,2;
- la raccolta delle schede è un `IObservableVector<FileExplorerTabItemControlVM>` che implementa
  `IVector<IInspectable>` (non `IBindableVector`): togliere e reinserire una scheda la riordina e la
  scheda attiva resta quella. I modelli delle schede notificano i cambiamenti (`INotifyPropertyChanged`),
  il titolo è il `TextBlock` dell'intestazione della scheda (uno `StackPanel`), l'icona un
  `ImageIconSource`. La striscia nascosta realizza tutte le schede (cache del suo `ItemsStackPanel`
  allargata), così titoli e icone ci sono anche con molte schede;
- (fino al 2026-11-05) la diagnostica XAML di WinUI 3 valeva per tutto il processo, con il nome della
  connessione variabile (`WinUIVisualDiagConnectionN`); è stata tolta perché tratteneva le finestre chiuse
  (vedi "Problema risolto");
- **Esplora file riusa le finestre chiuse** (stesso HWND per una finestra nuova): un pannello spento resta
  nascosto in una posizione qualunque, quindi l'isola dell'intestazione si cerca tra quelle **visibili**
  (la più in alta); prima del correttivo la tray, cercando la prima isola della finestra, finiva sul
  pannello, non leggeva più l'ordine delle schede e Ctrl+Maiusc+T perdeva una scheda riaperta;
- con explorer.exe chiuso a forza la sessione resta intera (verificato anche sul processo principale con
  una finestra di prova di 3 schede); **non** usare il messaggio non documentato di uscita da Esplora file
  (`WM_USER+436` a `Shell_TrayWnd`): su questa build non lo chiude e una finestra dell'utente ha perso
  una scheda prima della chiusura forzata (per riavviare Esplora file: chiusura forzata, poi "Riapri ora");
- explorer.exe appena riavviato (barra delle applicazioni, desktop e finestre che partono insieme) è
  lento per decine di secondi: un "Riapri ora" lanciato 3 s dopo ha impiegato 20 s, e un comando "nuova
  scheda" mandato in quel momento è andato perso (nessuna scheda, nemmeno in ritardo);
- sul sottomenu "Colore scheda" (`MenuFlyoutSubItem`) il pattern `ExpandCollapse` di UI Automation
  chiude l'intero menu invece di aprire il sottomenu; col mouse vero il sottomenu si apre normalmente;
- lo sfondo della finestra (Mica Alt) segue il tema di Windows, non quello dell'XAML. Cambiando
  `RequestedTheme` alla radice dell'intestazione (comando di prova `theme`), caselle e testi diventano
  chiari ma la barra del titolo resta scura. Per le prove conta solo la parte XAML: le risorse di tema
  si rivalutano come con un cambio di tema vero;
- Esplora file reimposta da sé lo sfondo della fascia degli indirizzi quando cambia il tema, ma non
  quello della fascia dei comandi. Per questo un valore sostituito restava quello del tema vecchio,
  mentre un'animazione sopra il valore originale no;
- le voci di primo livello del riquadro di spostamento **non** sono i figli del Desktop: enumerare il
  Desktop con `SHCONTF_NAVIGATION_ENUM` restituisce tutto il Desktop, file dell'utente compresi. Esplora
  file unisce gli elementi fissati nell'albero e le cartelle aggiunte ad Accesso rapido (vedi sopra).
  Nell'albero c'è una radice "Desktop" nascosta e, sotto, le voci visibili;
- elencare Accesso rapido (`{679f85cb-…}`) costa 200-400 ms, perché calcola anche frequenti e file
  recenti; "Posizioni frequenti" (`{3936E9E4-…}`) ha le stesse cartelle aggiunte, nello stesso ordine,
  in circa 30 ms;
- una cartella aggiunta con un nome fatto di U+2800 ("Braille vuoto") e un'icona trasparente è il
  trucco per distanziare le voci del riquadro di spostamento. Per Esplora file è una voce come le
  altre, e la barra la riporta così (nessun caso speciale);
- nello stesso processo, `SendMessage(ShellTabWindowClass, WM_USER+7)` (`CWM_GETISHELLBROWSER`) dà il
  browser della scheda anche con le schede di Windows 11, e il verbo `opennewtab` con quel browser come
  sito apre la scheda nella stessa finestra;
- l'ordine delle cartelle aggiunte ad Accesso rapido è quello delle "fissate" del jump list di Esplora
  file (AppID `Microsoft.Windows.Explorer`, file `f01b4d95cf55d32a.automaticDestinations-ms`). Non c'è
  un'API pubblica: la mod usa `IAutomaticDestinationList` di `windows.storage.dll`:
  - CLSID `{F0AE1542-F497-484B-A175-A20DB09144BA}`, IID `{E9C5EF8D-FD41-4F72-BA87-EB03BAD5817C}`;
  - ordine dei metodi letto dalla vtable di `CAutomaticDestinationList` con i simboli pubblici di
    Microsoft: Initialize, HasList, GetList, AddUsagePoint, PinItem, IsPinned...;
  - verificato a runtime: la vtable dell'oggetto è quella principale della classe;
  - un'interfaccia cambiata avrebbe un IID nuovo, com'è successo da Windows 7 a Windows 10.
- `PinItem(elemento, i)` su una cartella già fissata la sposta prima dell'elemento che ha l'indice `i`,
  contato prima di toglierla; con `i` uguale al numero delle fissate la mette in fondo. `IsPinned` dà
  l'indice. Le fissate comprendono anche file e cartelle virtuali che il riquadro non mostra: gli indici
  si chiedono sempre a `IsPinned`, non si contano nella barra;
- "Aggiungi ad Accesso rapido" (`pintohome`) mette in fondo. "Rimuovi da Accesso rapido"
  (`unpinfromhome`) funziona solo sull'elemento DENTRO Accesso rapido: sulla cartella stessa non fa
  nulla;
- avvisi della shell osservati (registrazione ricorsiva sul Desktop):
  - "Aggiungi ad Accesso rapido" e "Rimuovi da Accesso rapido" mandano `SHCNE_EXTENDED_EVENT` con codice
    0xF, poi `SHCNE_UPDATEDIR` sulla **Home** (`{F874310E-…}`);
  - `PinItem` manda solo l'evento esteso 0xF. Basta alla barra, che ascolta Accesso rapido e Posizioni
    frequenti, anche negli altri processi;
- i divisori del riquadro di spostamento nativo non sono voci dell'albero. Il riquadro raddoppia la riga
  della voce che viene dopo il divisore (80 px invece di 40 al 125%) e traccia la linea nella metà alta,
  20 px sotto l'inizio della riga (#383838 su #191919 nel tema scuro). Le voci con la riga doppia sono la
  prima cartella aggiunta e la prima voce dopo le cartelle aggiunte. Verificato leggendo i rettangoli
  delle voci e cercando le righe di pixel a linea, in una finestra senza mod. Una distanza in più prima
  di "Rete" non è un divisore: è un'unità sotto "Questo PC" aperto;
- il riquadro di spostamento nativo (NSTC) **non riordina** le voci che ha già. Non lo fa né dopo
  `PinItem`, né con l'evento 0xF, né con `SHCNE_UPDATEDIR` sulla Home, né con
  `SHCNEE_ORDERCHANGED` sulla Home o su Posizioni frequenti. Le finestre non si bloccano: misurato con
  `WM_NULL` ogni 100 ms. Al più aggiunge voci nuove;
- **mai `SHCNE_UPDATEDIR` su Posizioni frequenti o Accesso rapido** (`{3936E9E4-…}`, `{679f85cb-…}`):
  - fa rileggere tutto al riquadro di ogni finestra, sul suo thread: ha bloccato la finestra di prova per
    più di 5 s, e insieme ad Accesso rapido anche quella dell'utente per 20 s;
  - un riquadro con l'ordine vecchio ha **riscritto quell'ordine in Windows**. Le cartelle di prova sono
    tornate come le vedeva lui, prima degli altri elementi fissati che non mostra. Così si possono
    annullare spostamenti dell'utente;
- un trascinamento simulato di una cartella aggiunta nel riquadro di sistema (build 26300, finestra
  senza mod) non ha fatto nulla: nessuno spostamento, nessun avviso. Non c'è quindi un meccanismo di
  Windows da imitare. Le finestre aperte dopo prendono l'ordine nuovo;
- dopo una navigazione è Esplora file a dare il fuoco della tastiera alla vista nuova, dopo circa
  0,2 s. Darlo subito dopo `BrowseObject` lo mette sulla vista vecchia, che sta per essere distrutta;
- un flyout di WinUI con `ShouldConstrainToRootBounds(false)` sta in una finestra a parte
  (`Microsoft.UI.Content.PopupWindowSiteBridge`, di proprietà della finestra principale). UI Automation
  la mostra come figlia della finestra principale. Le coordinate di `GetCurrentPoint(elemento)` valgono
  anche fra l'isola e quella finestra: un trascinamento catturato da una delle due si posiziona
  sull'altra;
- prove col mouse in una sessione remota: un movimento sul PC dell'utente sposta il cursore, e la
  finestra in primo piano può cambiare durante la prova. Un Esc mandato con il terminale di Claude Code
  in primo piano ha interrotto il lavoro. Per questo c'è la guardia sul primo piano;
- "Riquadro di spostamento" (Visualizza > Mostra, build 26300) è **per scheda**:
  - Esplora file nasconde il `CtrlNotifySink` del riquadro (con `NamespaceTreeControl`) solo nella scheda
    attiva, e la vista dei file si allarga a sinistra;
  - le altre schede lo tengono acceso anche quando tornano in primo piano;
  - il registro (`PageSpaceControlSizer`, secondo DWORD) cambia subito ed è quello che ricevono le schede
    nuove: senza riquadro per 30-90 ms, poi con il riquadro già acceso o spento;
  - le finestre degli altri processi non cambiano;
  - con UI Automation la voce è un `MenuItem` con `TogglePattern`. Cercando solo per nome si trova prima
    l'albero del riquadro, che si chiama come lei. Dopo `Toggle` il menu resta aperto e si chiude con
    `Collapse` sul pulsante "Visualizza";
- menu contestuale nuovo (build 26300):
  - nel menu nuovo "Nuovo" è, per UI Automation, una foglia: Expand e Invoke non aprono il sottomenu,
    né nel riquadro né nella barra. Si riempie solo con il puntatore sopra;
  - il flag 0x8000 di `QueryContextMenu` (menu nuovo) fa caricare solo le estensioni che lo supportano;
  - la vista dei file e il riquadro creano il presenter con la finestra della scheda: la sua isola XAML
    (0×0, visibile) è figlia di `ShellTabWindowClass`;
  - leggere da fuori, con `MN_GETHMENU` e `GetMenuString`, il menu classico aperto da "Mostra altre opzioni"
    del **riquadro** chiude Esplora file (`abort`), anche senza mod. Lo stesso menu della barra, letto
    allo stesso modo, no. Quel menu del riquadro, poi, era quello di un'unità e non della cartella: il
    punto passato al riquadro non è quello della voce;
- due schede nuove una sopra l'altra (build 26300): invocando di nuovo il verbo `opennewtab` prima che la
  scheda di prima abbia la sua vista dei file, Esplora file cade (0xC0000005 in
  `CShellBrowser::QueryService`+0xFF, una volta su una con due cartelle). Lo stack, preso con la diagnostica
  delle prove (`dev-exceptions.txt`) e tradotto con i simboli:
  - `CShellBrowser::CreateViewWindow` della scheda nuova, e SHELL32 manda un messaggio alla finestra;
  - `CExplorerFrame::v_WndProc` → `SelectTab` → `_UpdateTargets` → `FireNavigationTargetChangedEvent`;
  - l'intestazione XAML chiede la vista attiva (`FileExplorerNavigationController::ActiveView`), e
    `CBrowserHost::QueryService` passa a un `CShellBrowser` non più valido.

  Aspettando la vista della scheda di prima (circa 0,6 s) non succede: 8 schede di fila, una alla volta,
  senza problemi. Con 1,5 s fissi fra le due nemmeno; con la stessa scheda di partenza per tutte ma senza
  pausa sì;
- subito dopo un riavvio di Esplora file una finestra chiusa può restare viva, nascosta, per circa 10 s.
  Probabilmente Esplora file non ha ancora una finestra pronta da riusare e tiene quella. La tray la
  considera chiusa solo quando sparisce: la suite principale lanciata 40 s dopo un riavvio ha fallito
  "Chiuse insieme" per questo, e mezz'ora dopo ha dato 64 su 64.

Limiti dell'intestazione del colore della barra del titolo: vale solo con le schede verticali (con quelle
orizzontali l'intestazione resta originale); le due linee sottili fra le barre e sotto la barra dei
comandi restano (sono bordi, non sfondi).

Limiti del riquadro di spostamento in alto:
- Solo il primo livello, come la barra dei segnalibri: le sottocartelle non si espandono nella barra
  (ci sono le frecce della barra degli indirizzi).
- Trascinare file su una voce per spostarli lì non funziona: il trascinamento OLE non raggiunge un'isola
  XAML nostra, come per le righe delle schede verticali.
- I gruppi raccolgono solo cartelle aggiunte ad Accesso rapido (e altri gruppi), non Home, Galleria,
  Questo PC, Rete... che restano al loro posto, come nel riquadro.
- "Apri tutte" apre una scheda alla volta (circa una al secondo), per il difetto di Esplora file con due
  schede nuove di seguito (vedi note tecniche).
- Una cartella tolta da Accesso rapido fuori dalla barra (Home, riquadro di sistema) resta ricordata nel
  suo gruppo, invisibile: riaggiunta, torna lì.
- Una cartella aggiunta o tolta da Accesso rapido compare nella barra:
  - subito, se la shell lo notifica (e sempre se lo si fa dal menu della barra);
  - altrimenti al più tardi quando si torna sulla finestra (riletture al più ogni 30 s) o entro 5 minuti.
- Il riquadro verticale è solo nascosto, non tolto: con F6 il fuoco della tastiera può passarci, anche se
  non si vede.
- Il riquadro di spostamento di sistema delle finestre già aperte non rilegge mai l'ordine: la mod lo rimette
  in pari riordinando il suo albero (vedi "Gruppi"), ma solo nelle finestre dei processi dove è caricata. Con
  le schede verticali sospese per sicurezza, o in un processo senza la mod, resta nell'ordine vecchio finché la
  finestra non si riapre. L'avviso della shell che lo farebbe rileggere blocca le finestre e può riscrivere in
  Windows l'ordine vecchio (vedi note tecniche): non si usa.
- Trascinando nell'elenco "»" lungo, l'elenco non scorre da solo verso il bordo: lo si scorre prima con
  la rotellina.

**Trascinando da un gruppo a un altro (2026-11-06, 12:19).** Trascinando una cartella o un gruppo dall'elenco di un
gruppo e fermandosi su un altro gruppo della barra (o su "»", o su un sottogruppo accanto), il suo elenco si apre al
posto di quello di partenza e la voce si lascia dove si vuole, come in Chrome. Prima quel caso era escluso: chiudere
l'elenco di partenza annullava il trascinamento. Ora il trascinamento si "stacca" (vedi "Gruppi" nelle note
tecniche). Prove:
- `Test-NavGroups.ps1` 62 su 62 senza mouse;
- col mouse vero (`-MouseOnly`) 19 su 19: dall'elenco di xpxk, fermi su xpxC, la cartella va in cima al suo
  elenco, e il trascinamento si stacca circa 0,9 s dopo l'inizio.

Nello stesso giro:
- un gruppo nella barra non si rifà più quando cambia solo il suo contenuto (prima la voce appena lasciata in un
  elenco lo chiudeva);
- nella prima prova col mouse vero un clic su un gruppo, circa 1,5 s dopo la chiusura programmata di un elenco
  aperto passandoci sopra, non ne ha aperto l'elenco. Non si è ripetuto né col mouse né con UI Automation (controllo
  apposta). Da tenere d'occhio.

Nel processo della barra delle applicazioni dal riavvio delle 12:19: 1 finestra con 3 schede e il colore, come prima.

**Gruppi della barra come Chrome (2026-11-06, 11:11).** Con l'elenco di un gruppo aperto, passando sopra un altro
gruppo (o su "»") il suo elenco si apre subito, senza la sosta e senza animazioni; il pulsante del gruppo aperto
resta evidenziato. Misurato con il cursore di prova: 0-32 ms dal comando (la granularità del timer), prima fino a
circa 250 ms più le animazioni. `Test-NavGroups.ps1` 55 su 55 senza mouse (a 225% la finestra di prova mostra 9
voci: il gruppo vuoto finiva in "»" e il blocco nuovo lo porta in testa per la sua durata). Nel processo della
barra delle applicazioni dal riavvio delle 11:11 (1 finestra con le sue 3 schede e il colore, come prima; la
finestra nascosta di riserva di Esplora file non era nella sessione).

**Correzione nel processo dell'utente (2026-11-05, 19:05).** La mod senza diagnostica XAML (`Discovery.cpp`) è attiva
nel processo della barra delle applicazioni dal riavvio di Esplora file delle 19:05 (finestra dell'utente tornata
con le sue 4 schede, trovata dalla ricerca al primo tentativo). Verifiche: tutte le prove delle schede verticali
(Basics 36, Panel 25, Header 19, Colors 16, NavBar 46, NavGroups 46, Guard 12, ForcedClose 3/3, Discovery 9),
suite principale 64/64, Test-OpenInTabs 19/19; nel processo della barra, decine di finestre aperte e chiuse
dalla suite principale senza thread in più (208-223, prima arrivava a 333); i processi explorer.exe "/factory" con
la mod escono da soli (7 su 7 entro 41 s); confronto in un explorer.exe di prova con 12 finestre aperte e chiuse:
la memoria cresce di circa 2,5 MB per finestra con la mod e 2,7 senza (è Esplora file stesso), più circa 14 MB una
volta sola con la mod (pannello, barra, icone). Corretto nello stesso giro il log della mod: il file si apre a ogni
riga (prima un handle aperto per sempre: quando un altro explorer.exe ruotava il log, gli altri processi
continuavano a scrivere nel file rinominato) e la dimensione si ricontrolla ogni 256 righe; e la visibilità di
pannello e barra non tocca più il ponte di un'isola la cui finestra è già distrutta (`IsWindow` prima di
`Show`/`Hide`, il crash trovato in VTabsNotepad).

**Problema risolto (2026-11-05): i thread delle finestre chiuse restavano vivi.** Con la mod caricata, ogni
finestra di Esplora file chiusa lascia vivo il suo thread in explorer.exe (con il suo XAML, decine di MB):
il processo della barra cresce a ogni finestra aperta e chiusa (misurati 333 thread e 1,3 GB dopo una giornata
di prove), e un explorer.exe "/factory" (le cartelle aperte con `explorer.exe <cartella>`) non termina più
dopo la sua ultima finestra (circa 300 MB ciascuno; senza la mod esce dopo circa 60 s). Causa, trovata con
`stackdump` (stack dei thread con i simboli pubblici Microsoft) e il sorgente di WinUI 3: il thread della
finestra resta in `CExplorerFrame::FrameMessagePump`, che esce solo quando il contatore dei thread ref della
finestra (`SHCreateThreadRef`, `CExplorerFrame+0x128`) torna a zero; ma gli oggetti che li tengono sono
trattenuti dalla cache della **diagnostica XAML** (`RuntimeObjectCache`: le radici, cioè i
DesktopWindowXamlSource delle intestazioni, sono tenute con riferimenti forti, e la cache si libera solo alla
deinizializzazione del thread, che non arriva mai). Bisezione con `dev-mod.txt` (interruttori solo di
sviluppo, vedi ModCore.cpp): senza diagnostica nessun thread resta; con la diagnostica attiva anche senza
agganciare nessuna finestra sì; Advise subito seguito da Unadvise non basta. Non si può chiudere la sessione
dall'esterno senza rischi (il nucleo XAML punta all'istanza con un puntatore grezzo), e togliere il contenuto
dell'intestazione alla chiusura fa andare in crash Esplora file (provato in un processo di prova).
**Correzione:** la mod non usa più la diagnostica XAML; trova le barre delle schede con gli agganci di
`Discovery.cpp` (vedi "Aggancio" in "Come funziona"). Prove: `Test-Discovery.ps1` (finestra già aperta trovata
dalla ricerca, finestra nuova con Ctrl+N trovata da OnApplyTemplate, schede verticali in tutte e due, nessun
thread vivo dopo la chiusura) e `Measure-ThreadLeak.ps1` (apre e chiude una finestra di prova in un
explorer.exe separato per ogni configurazione, es. `-Configs 'vt+nav', 'none'`, e conta i thread rimasti vivi:
ora 0 in tutte). Strumento per gli stack: `tools\stackdump\stackdump.cpp` (istruzioni in testa al file).

**Cartelle trascinate da fuori sulla barra** (2026-11-07, 1.3.0): prova con il mouse vero nella finestra
dimostrativa (explorer.exe separato, `C:\VTabs Demo`), dalla vista dei file alla barra:
- cartella nuova tra due cartelle aggiunte: aggiunta lì;
- al centro di un gruppo: dentro;
- prima di un gruppo: inizio del blocco;
- un file: rifiutato, con il cursore di divieto;
- una cartella già aggiunta: spostata, non aggiunta di nuovo.

Le cartelle trascinate restano dove sono (stesso numero di elementi), il registro non ha errori e le fissate
dell'utente sono identiche dopo la prova (impronta). In modalità dimostrativa l'ordine di Windows non si tocca: una
cartella già aggiunta spostata al primo livello lascia i gruppi ancorati alle cartelle di prima, come il
trascinamento dalla barra. Fuori dalla modalità dimostrativa l'ordine di Windows segue.

**Elenchi aperti trascinando da fuori** (2026-11-07, 1.4.0): stessa finestra dimostrativa, mouse vero:
- fermi su Personal l'elenco si apre; lasciata tra due righe: aggiunta lì, nel gruppo;
- fermi su Work e poi sulla riga del sottogruppo Clients: si apre il sottoelenco; lasciata in fondo: in Clients;
- Work, poi Personal: si apre il suo al posto di quello di Work; trascinando via da barra ed elenchi si
  chiudono; annullato con Esc: nulla aggiunto;
- una cartella già aggiunta lasciata in cima all'elenco di un gruppo: spostata lì;
- finestra stretta, fermi su "»": l'elenco si apre; lasciata in fondo: al primo livello;
- da un altro processo (una finestrina WinForms che dà solo `CF_HDROP`): l'elenco si apre, il trascinamento
  continua dentro l'elenco (un solo inizio nel registro), aggiunta lì con l'effetto "collegamento"; tornando
  sulla sorgente l'elenco si chiude e il rilascio lì non aggiunge nulla.

Dopo ogni rilascio gli elenchi si chiudono, e ogni finestra di elenco aperta riceve il bersaglio (0x00000000, senza
sostituirne uno). Attenzione nelle prove: le chiavi delle cartelle tolte da Accesso rapido restano nei gruppi
(nascoste), quindi una prova ripetuta trova la cartella già al suo posto; per una prova pulita si rifà la demo.

Limiti delle schede verticali: trascinare un file sopra una riga per spostarlo in quella scheda non
funziona (il trascinamento XAML non raggiunge un'isola XAML aggiunta da noi, come in VTabsNotepad; un
bersaglio OLE registrato sull'isola sì, lo usa la barra delle cartelle dalla 1.3.0, ma per le righe non c'è); una
scheda non si stacca in una nuova finestra trascinandola; la prima finestra dopo l'avvio di Esplora file
mostra le schede orizzontali per un paio di secondi, finché la tray non vi porta la mod.

Limiti: una sessione aperta su cartelle di rete non raggiungibili può rallentare il ripristino (la shell
aspetta la rete; dopo 10 s la finestra compare comunque e le ultime schede arrivano a vista); la
posizione delle schede dentro una finestra segue il nome mostrato, quindi con due schede dallo stesso
nome l'ordine fra loro è quello della shell; le finestre di un explorer.exe diverso da quello della barra
(opzione "Apri le finestre delle cartelle in un processo separato", o cartelle aperte con
`explorer.exe <cartella>`) il velo le vede solo quando compaiono, non quando nascono: restano visibili per
qualche millisecondo prima di essere velate.

## Disattivare o rimuovere

Dal menu della tray: togliere "Riapri le cartelle all'apertura di Esplora file", oppure "Esci". Le
schede verticali si spengono dalla loro voce (torna subito la barra orizzontale; la mod resta caricata
in Esplora file, inattiva, fino al suo riavvio). Per rimuovere tutto: spegnere le schede verticali,
chiudere la tray, riavviare Esplora file, togliere "Avvia con Windows", cancellare
`%LOCALAPPDATA%\VTabsExplorer` e la cartella del progetto. Su disco Esplora file non viene mai modificato.

## Lingua dei testi (dalla 1.1.0)

I testi per l'utente (menu e avvisi della tray, menu "Colore scheda" e nomi dei colori, menu dei gruppi, riquadro
del nome, suggerimenti, nomi per l'accessibilità) esistono in italiano e in inglese. `UiItalian()` in `ModShared.h`
legge `GetUserDefaultUILanguage()` una volta per processo, nella tray e nella mod: italiano se la lingua di
visualizzazione di Windows è l'italiano, altrimenti inglese (cambiarla richiede di disconnettersi).
`Tr(inglese, italiano)` sceglie il testo. Il markup XAML resta scritto in italiano: in inglese i valori degli
attributi per l'utente si sostituiscono prima di `XamlReader::Load`. I log restano in italiano. L'installazione
sceglie la lingua allo stesso modo (Inno Setup, `ShowLanguageDialog=no`).

Le prove cercano i testi dell'interfaccia (della mod e di Windows: menu, riquadro del nome, "Altre cartelle",
"Questo PC") con `Ui 'inglese' 'italiano'` del TestKit, che segue la lingua di Windows come `Tr()`. Le righe del
registro che le prove leggono sono sempre in italiano. Dalla 1.4.0 `Test-NavGroups.ps1` passa anche con Windows in
inglese; le altre prove di Esplora file possono avere ancora qualche nome italiano.

Non affiliato a Microsoft.

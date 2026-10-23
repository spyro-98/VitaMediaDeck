# Correzioni dal feedback console — 22 ottobre 2026

Pacchetto: `build/ui-fixes-20261022/VitaMediaDeck.vpk`.
SHA-256: `0d82faeaed1d968f48b90f2579932c3ae137d5d728dc2809eda40c59eb46fe3a`.

## Modifiche

- **Anteprime dopo il player:** il ritorno da un player aperto attraverso Start o il titolo del mini-player riprende ora il worker delle anteprime, anche quando il browser non viene ricreato. I browser locali ripristinano il servizio prima del disegno. La ripresa è idempotente: le chiamate ripetute non cancellano la preemption di una richiesta in corso. Una nuova sessione del browser può ritentare subito le anteprime fallite sotto pressione di memoria.
- **Home:** il carosello occupa la zona libera della barra superiore, tra logo e indicatori. Il tab selezionato si centra nell'area del carosello; quelli vicini restano visibili. Navigazione animata con Sinistra/Destra e swipe; i tocchi seguono la posizione effettivamente disegnata. Sono recuperati 48 pixel verticali. La preferenza Riduci movimento elimina l'animazione. Ordine personalizzato conservato.
- **Cartelle:** Home e browser usano ora lo stesso simbolo, con proporzioni costanti, eliminando il rettangolo schiacciato della Home.
- **Immagini:** niente rotazione continua dalla levetta destra o dal gesto a due dita. Rotazione esclusivamente di 90 gradi. Pan senza inerzia, velocità basata sul tempo, zona neutra della levetta ampliata, letture controller senza campioni neutralizzate. La foto resta nei limiti utili del viewport. I gesti seguono le identità dei contatti, anche se il touch cambia ordine o dito. Il fit e il dettaglio nativo tengono conto della rotazione.
- **Seek:** una copia RGB565 a 480×272 dell'ultima schermata visualizzata resta visibile con spinner durante il caricamento. Occupa circa 255 KiB, è indipendente dalle superfici del decoder e viene rilasciata dopo la fine dell'operazione. Se la cattura o l'allocazione fallisce, resta disponibile il caricamento ordinario. La copia può includere l'HUD visibile prima del seek.

Il seek esistente ferma e riavvia parti del decoder, riposiziona i demuxer e attende il primo frame. Questo percorso è conservato: questa correzione migliora la continuità visiva, senza dichiarare una riduzione dei tempi reali, che richiede misure sulla console.

## Comandi immagini

| Comando | Azione |
| --- | --- |
| Triangolo / X | Zoom avanti / indietro |
| Quadrato | 90 → 180 → 270 → 0 gradi |
| D-pad / levetta sinistra | Sposta il punto di vista; si ferma al rilascio |
| Trascina / pizzica | Sposta la foto / zoom |
| START | Ripristina zoom, posizione e rotazione |
| Tocco / Cerchio | Mostra o nasconde HUD / torna indietro |
| L1 / R1 | Sezioni / informazioni |

## Verifiche eseguite sul Mac

- 22.704 controlli sui comandi immagini: jitter, rilascio, 30/60 Hz, frame ritardati, bordi, 1.000 rotazioni, pinch, cambio contatti.
- 16.637 controlli sui tab e tutti i 720 ordinamenti: ripetizione tra schermate, centratura animata, swipe, spazi tra tab, margini e Riduci movimento.
- 19 controlli sul ciclo delle anteprime, 6 sul ritorno effettivo dal mini-player e 36 su pixel/risorse/errori del fermo immagine per il seek.
- UI di produzione con rasterizzazione host: 452 controlli Home, 1.703 audio, 2.720 impostazioni/guida/azioni, 319 download e nuovo overlay del seek. Layout italiano/inglese ispezionati. Le immagini QA usano cartelle e sfondi di prova, senza contenuti della console.
- 29 controlli di navigazione Radio/Podcast; suite focus e preferenze superate.
- AddressSanitizer e UndefinedBehaviorSanitizer per i nuovi test host: superati.
- Build Release ARM Vita: completata senza warning del compilatore. Audit sorgenti/TLS/provenienza/licenze, CRC dell'archivio e corrispondenza `eboot.bin`/SELF compilato: superati.

Queste prove non eseguono GPU, input, filesystem o decoder reali della Vita. Vita3K non fornisce una sessione affidabile per la verifica visiva; nessun esito del simulatore viene presentato come prova hardware. Il buon esito dei player HW/SW, tracce e sottotitoli riportato dall'utente si riferisce alla build precedente.

## Quattro verifiche sulla Vita

1. **Anteprime:** apri un video dalla cartella e torna con Cerchio; le anteprime devono tornare senza uscire dalla cartella. Ripeti dopo aver minimizzato con Start e riaperto sia con Start sia toccando il titolo del mini-player. Prova anche un secondo video.
2. **Home:** Sinistra/Destra, pressione prolungata, swipe e tocco dei tab. Verifica cartelle proporzionate, nessuna sovrapposizione con stato/batteria, Su/Giù tra tab e contenuto, Radio/Podcast e ordine personalizzato. Controlla anche Riduci movimento e l'eventuale mini-player.
3. **Immagini:** zooma, sposta con levetta e touch, poi rilascia: la foto deve fermarsi. Quattro pressioni di Quadrato devono tornare all'orientamento iniziale. Verifica fit a 90/270 gradi, pinch senza rotazione, START e dettaglio delle immagini grandi dopo averle ruotate.
4. **Seek HW e SW:** prova avanti/indietro e barra temporale, in riproduzione e pausa, anche vicino alla fine. Durante l'attesa deve rimanere il fermo immagine con spinner, seguito dal nuovo frame. Controlla audio/sottotitoli e annullamento con Cerchio. Per valutare la velocità confronta lo stesso file e gli stessi punti; il log di sessione contiene le fasi del seek.

Nessun commit o push eseguito. Le modifiche precedenti nel checkout sono conservate.

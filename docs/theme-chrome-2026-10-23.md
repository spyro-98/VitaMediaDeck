# Nitidezza, barra di stato, L1 e posizioni Home — 23 ottobre 2026

## Resa e coerenza

L'output resta nativo 960×544. Il renderer viene inizializzato con il percorso
MSAA 2× di vita2d/GXM, senza un framebuffer 1080p da ridimensionare. L'obiettivo
è migliorare i bordi della geometria e della scena Ghost mantenendo i font
nelle rispettive istanze alla dimensione corretta. Le trasformazioni 3D e il
batching introdotti nella revisione precedente restano invariati.

Il percorso usato è presente nell'header e nella libreria VitaSDK locali;
[l'implementazione di libvita2d](https://github.com/xerpi/libvita2d/blob/master/libvita2d/source/vita2d.c)
mostra il render target nativo e la configurazione multisample. Una build
riuscita non conferma il costo in frame time o memoria sulla console.

La direzione resta quella già scelta: Ghost usa sfondo `#081014`, superficie
`#102329`, lime `#B8F27D`, ciano `#64B9BF`, testo `#DFEFDF`. Inter è il carattere
principale, Inter SemiBold per i titoli; Deck 90 usa IBM Plex Mono. I controlli
restano allineati alle strutture di ciascun tema. Non si aggiungono valori o
telemetria decorativi.

La barra di stato ora ha una struttura per tema: moduli separati e tagli obliqui
in Ghost, terminale in Deck 90, fondo chiaro e accento vermiglio in Signal,
blocchi sospesi in Aperture, telaio tecnico in Relay, pannelli viola in Mosaic.
Il comando L1 è esplicito e il nome dell'app rimane leggibile. Icone, attività
ed energia usano la palette del tema; avviso, errore e ricarica mantengono un
significato distinto. Batteria, orologio e rete provengono ancora dalle API
reali con cache di due secondi, senza richieste di sistema a ogni frame.

## L1

Nei sei temi nuovi il vecchio cassetto laterale è sostituito da un pannello
ampio con tre colonne. Due colonne contengono Home, rete, Navidrome, Immich,
Radio/Podcast e IPTV; la terza impostazioni, guida e informazioni. Player e
download attivi hanno una fascia separata. La barra di stato resta visibile
sopra il pannello. Il materiale e gli elementi di cornice seguono il tema.
Anche la pagina di disposizione del player usa il nuovo pannello.

Su/giù percorrono le voci; sinistra/destra passano fra colonne quando il player
non ha abilitato la propria pagina di disposizione. In quel caso destra/sinistra
mantengono il comando precedente per passare fra Sezioni e Player. Touch,
rendering e fuoco condividono le coordinate. Gli identificativi delle sezioni
non sono cambiati.

Legacy mantiene il cassetto. Le categorie hanno più spazio, titoli evidenziati
e separatori che non attraversano il testo. Tutte le voci restano sopra il
footer anche con player e download presenti contemporaneamente.

## Libreria e ordine dei tab

Nella Libreria generale i file/cartelle locali precedono Navidrome e Immich.
I servizi sono in coda anche nella vista aggregata dei file. In Audio locale
Navidrome resta il primo elemento; in Immagini Immich resta il primo. Con
una libreria vuota i collegamenti ai server restano accessibili.

**Impostazioni → Interfaccia → Posizione tab del tema** apre l'editor relativo
al tema attivo, indicato anche nella riga delle impostazioni. Ogni voce mostra
la destinazione reale: settore della ruota in Ghost, riga in Deck 90, cella
in Relay/Mosaic, posizione della barra in Signal/Aperture/Legacy. Su/giù sceglie
la voce, sinistra/destra o i controlli touch la spostano; X salva, Cerchio annulla.

Ogni tema conserva un file `home-tabs-<id>.bin` indipendente, con temporaneo e
backup. L'ordine globale precedente rimane intatto e viene usato come base
per i temi non ancora configurati. È accettato anche il vecchio formato a
sette tab. Il cambio tema conserva il tab semanticamente selezionato.

## Verifica

- `tests/home_tabs/run-host-tests.sh`: tutte le 720 permutazioni, sette file
  indipendenti, migrazione, cambio tema, fallimento del salvataggio e backup.
- `tests/ui_chrome/run-host-tests.sh`: codice reale di header, stato, Home,
  L1 ed editor; sette temi, due lingue, 12/24 ore, attività simultanee, stato
  sconosciuto, cache delle letture e editor con salva/annulla/errore simulato.
- `tests/ui_crossfade/run-game-ui.sh`: rettangoli e dispatch di L1, pagina
  del player, provider dopo/prima dei locali secondo categoria, mesh e
  controllo di Riduci movimento.
- Regressioni layout, radio/podcast, cache, servizi personali e ritorno player.

I PNG sono rasterizzazioni host con dati sintetici: non catture della Vita.
Il rasterizzatore host non dimostra l'effetto MSAA hardware. Restano sulla
console: avvio con MSAA, fluidità e memoria, riproduzione HW/SW concorrente,
tastiera nativa, leggibilità OLED/LCD e prova fisica di touch e controlli.

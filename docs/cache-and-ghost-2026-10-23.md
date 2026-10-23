# Miniature, navigazione da cache e Ghost Mesh — 23 ottobre 2026

## Copertine e liste

Tutte le righe media di Radio, Podcast, programmi ed episodi mostrano una
miniatura anche nei layout compatti dei sei temi nuovi. Legacy conserva le
copertine nelle schede e nelle liste; Aperture conserva anche la preview grande.
Quando una copertina non esiste o non è ancora decodificata, il riquadro resta
presente con FM/PD. Il primo episodio può ereditare l'immagine del programma.
I feed aggiunti direttamente possono recuperare l'immagine dal canale RSS/Atom
in background, senza dover aprire prima l'elenco degli episodi.

La cache raster condivisa rimane limitata a 128 immagini / 64 MiB, 2 MiB per
immagine, con validazione del formato ed espulsione dei file meno recenti. Il
worker delle miniature mantiene la cache di texture e decodifica le immagini
fuori dal disegno. Non si aggiunge un download per ogni frame.

Le Home Radio e Podcast conservano ciascuna fino a 64 preferiti in memoria,
comprese le copertine già risolte. Il cambio tab mostra quel contenuto e
rilegge i preferiti sul worker. I cataloghi e gli episodi hanno anche snapshot
persistenti: massimo 16 file da 512 KiB leggibili, indicizzati per tipo,
ricerca, paese, modalità e pagina. Una riapertura mostra lo snapshot prima
del refresh in background; una prima richiesta senza cache usa ancora il
caricamento iniziale annullabile. I nomi dei file sono digest SHA-256.

Il refresh conserva la selezione per URL e le immagini valide. Errori e
cancellazioni non eliminano l'ultima lista valida. I file sono sostituiti con
un temporaneo e backup per i filesystem che non sovrascrivono in rename.
Le risposte della generazione precedente non modificano la nuova schermata.
Le operazioni di rete mantengono i propri buffer fino al completamento; uscire
dalla sezione richiede la cancellazione ma non attende l'I/O. Si riusano i
buffer solo dopo il segnale di fine del worker.

Il player parte con la copertina già disponibile, senza aspettare un download
aggiuntivo della sola immagine. La risoluzione di un programma in URL del feed
rimane una richiesta esplicita quando il catalogo non contiene quell'URL.

## Passaggio a Immagini

L'indice e il riepilogo delle cartelle restano in memoria. Il worker calcola
conteggi, dimensioni e preview mentre scrive l'indice, evitando di rileggere
linearmente tutti i record sul thread UI al ritorno dai podcast. Se esiste
una cache su disco viene usata al primo accesso. Senza indice il primo frame
mostra la Home mentre parte la scansione. I refresh automatici hanno un
intervallo minimo di 60 secondi; l'azione esplicita di scansione lo ignora.

Anche dopo rinomina/eliminazione il refresh è differito. Una generazione
incrementale impedisce alla scansione avviata prima dell'operazione di
pubblicare i vecchi percorsi. Durante il breve refresh può restare visibile
un elemento precedente alla modifica, finché arriva il riepilogo completo.

## Ghost Mesh e frame

Il nucleo è ora una mesh 3D a 20 facce con luce direzionale. Tre anelli hanno
orientamenti tridimensionali distinti, prospettiva e rotazione continua; due
guide esterne condividono il centro dei sei nodi. I nodi hanno raggio 164 px
e intervalli di 60°, con il solo arrotondamento ai pixel. Il cursore segue
l'arco più breve con interpolazione temporale, senza ritardare l'azione.

Le trasformazioni e la proiezione prospettica sono calcolate dalla CPU;
la rasterizzazione dei triangoli è accelerata da GXM tramite vita2d.
La scena usa **4 batch, 1.692 vertici**, copiati nel pool di memoria GPU del
frame (circa 27 KiB). Non usa shader personalizzati, un motore 3D esterno o
una texture video. Il buffer CPU non viene riusato come memoria GPU pendente.
La mesh convessa ordina le facce; gli anelli sono divisi in passate anteriore
e posteriore. Riduci movimento conserva una posa statica.

La rotazione precedente usava un indice intero su 96 punti: la geometria
avanzava a scatti anche con un framerate regolare. Ora il movimento è continuo.
Nel fixture Home, le chiamate geometriche massime scendono da 844 a **308**.
Il budget del loop online include il tempo di rendering/vsync, invece di
aggiungere sempre altri 16 ms dopo ogni frame.

## Prove e limiti

- Cache persistente: chiavi indipendenti, metadati, annullamento, corruzione,
  scritture concorrenti e limite di 16 file, con ASan/UBSan e ThreadSanitizer.
- Navigazione: worker deliberatamente lasciati pendenti durante cambio tab,
  scarto delle risposte vecchie, cache immediata e refresh fallito.
- UI: sette temi nelle regressioni esistenti; miniature compatte dei sei temi
  nuovi in due lingue e con/senza mini-player, inclusi episodi e radio.
- Geometria: regolarità/opposizione dei nodi, limiti dei batch, movimento
  subpixel tra frame consecutivi e determinismo di Riduci movimento.
- Riepilogo locale: tipi, dimensioni oltre 4 GiB, scelta della preview e
  esclusione delle voci provider dall'indice dei file.

I PNG/GIF dei test sono rendering del codice nativo sul Mac con contenuti
sintetici, non catture da PS Vita o Vita3K. I conteggi non provano FPS, consumo,
memoria di picco o tempo di cambio tab sulla console. Restano da misurare
sulla Vita i passaggi rapidi, il caso offline/server lento, le immagini reali,
il playback contemporaneo e la fluidità della nuova scena GPU.

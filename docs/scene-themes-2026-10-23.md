# Sei interfacce spaziali — 23 ottobre 2026

Il renderer nuovo riguarda i sei temi non Legacy. Non cambia la risoluzione nativa
960×544. Legacy mantiene palette, geometrie e controlli precedenti; le anteprime
host della sua Home/barra di stato e del suo L1 risultano identiche al pacchetto
`hifi-chrome-20261023`.

Le composizioni di Aperture, Relay e Mosaic sono state aggiornate dopo le prove
su console: [aggiornamento](themes-player-feedback-2026-10-23.md).

## Composizioni implementate

| Tema | Navigazione e cartelle | Audio | Video |
| --- | --- | --- | --- |
| Ghost Mesh | Ruota esistente; archivio a due colonne; comparsa orbitale e profondità dei pannelli | Copertina centrale fra anelli e nucleo tridimensionale | Cornice esagonale, moduli del transport separati |
| Deck 90 | Indice verticale sinistro; terminale fitto; ingresso lungo l'asse della profondità | Copertina in prospettiva a sinistra e tunnel di nervature | Transport compatto e pannello con scansione da terminale |
| Signal | Grandi pannelli a coppie; serrande tridimensionali; apertura laterale e su cerniera | Copertina inclinata a sinistra e pannelli controrotanti | Tagli diagonali e fascia di controllo dedicata |
| Aperture | Un elemento grande, categorie inferiori; portali lungo la profondita; browser a scheda panoramica | Copertina centrale e tunnel prospettico | Moduli separati e telaio ottico |
| Relay | Categorie sul tracciato sinistro, stazioni collegate a destra; browser sfalsato | Copertina a destra e rete tridimensionale | Transport spostato e collegamenti industriali |
| Mosaic | Cinque copertine in una fascia, tab superiori; schedario a soffietto | Copertina a sinistra e fogli incernierati | Composizione a moduli e cornici angolari |

Le cartelle locali e di rete condividono geometria, transizione, miniature in
cache e rettangoli touch. Le scelte griglia/lista restano salvate per tema. La
lista resta una presentazione densa su una colonna. Radio e podcast usano le
composizioni Home del tema e conservano l'architettura cache/refresh esistente.
I tab riordinati mantengono anche le icone della categoria effettiva.

Il menu L1 ha disposizioni distinte: sei nodi orbitali più colonna applicazione,
terminale, pannelli, registro prospettico, nodi in serpentina e schedario.
L'apertura riparte a ogni riapertura. Il menu video e la timeline usano le stesse
coordinate per rendering e touch. Il decoder e il percorso del seek non cambiano.
Nel visualizzatore foto il modello del tema compare nella fascia informazioni:
zoom, pan, dettaglio nativo e rotazione restano quelli già corretti.

Nell'audio l'opzione precedentemente chiamata “Copertina centrata” diventa
“Composizione del tema” nei sei temi nuovi. Le altre due scelte mantengono il
posizionamento esplicito a sinistra/destra. Legacy mantiene le tre scelte originali.
La copertina è una superficie texturizzata proiettata nello spazio, con ritaglio
quadrato, senza deformare il rapporto originale. I modelli audio e video seguono
la pausa; i fondali ambientali restano ambientali. Riduzione del movimento blocca
le animazioni decorative e completa subito le transizioni. I colori audio del
menu R1 rimangono utilizzabili sopra la composizione del tema.

## Renderer e budget

`theme_scene.c` trasforma e illumina la geometria sulla CPU e invia triangoli a
GXM tramite vita2d. Le facce vengono ordinate tramite un vettore di indici, non
copiando strutture durante l'ordinamento. Ogni cubo riusa una base trigonometrica;
gli anelli riusano una circonferenza precalcolata. Il limite di ciascun modello è
192 quadrilateri / 1.152 vertici / un batch. Il nucleo Ghost preesistente usa quattro
batch e 1.692 vertici. Il costo totale di una schermata comprende fondale, modello,
pannelli, testo e copertina: questi limiti non sono una misura del tempo di frame.

I buffer sono statici sulla CPU e vengono copiati nel pool GPU del fotogramma
prima della submission: vita2d conserva il puntatore ai vertici. Anche i vertici
texturizzati usano quel pool. Nessun nuovo framebuffer, caricamento di shader,
allocazione heap o download viene richiesto dalla passata grafica. Il contesto
GXM e MSAA 2× già esistenti sono riutilizzati. Riferimento dell'API:
[libvita2d, implementazione delle texture](https://github.com/xerpi/libvita2d/blob/master/libvita2d/source/vita2d_texture.c).

## Verifiche e limiti delle prove

Le suite host usano i sorgenti di produzione con rasterizzatore e input sostitutivi,
ASan e UBSan. Controllano geometrie finite, modelli su tutti i temi non Legacy,
assenza di triangoli su Legacy, pausa/ripresa, riduzione del movimento, ritorni
rapidi fra cartelle, assenza di sovrapposizioni touch lungo tutta l'apertura,
mini-player, menu L1 con player/download, riordino e persistenza, flussi radio e
podcast e ciclo di vita delle anteprime durante il seek.

Le PNG e le sequenze sono render host con dati sintetici, non schermate della
console. Le immagini `scene-*-hud` mostrano soltanto la passata grafica condivisa
del video, non un film decodificato con tutti i suoi indicatori. Le anteprime audio
eseguono il vero renderer del player con un sostituto della riproduzione.
Build, audit e hash del VPK non provano fluidità, temperatura, consumi, qualità
sullo schermo Vita o concorrenza reale fra GPU e decoder. Queste verifiche sono
elencate nel file `PROVE-SU-VITA.txt` allegato al pacchetto.

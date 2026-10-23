# Correzioni dopo i test sulla console — 23 ottobre 2026

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

## Durata e seek MP4

Riprodotto con FFmpeg 6 reale: `avformat_open_input` restituisce MP4 H.264/AAC
con `format->duration == AV_NOPTS_VALUE`, ma tracce con durata di 24 secondi.
Il percorso rapido saltava `avformat_find_stream_info` per evitare probing
costoso, percio la UI riceveva durata zero e disabilitava timeline e seek.

Entrambi i pacchetti ora calcolano la durata dai metadati A/V quando manca quella
aggregata. Usano la stessa origine temporale, escludono immagini allegate e
gestiscono metadati mancanti/limite senza stimare secondi dal bitrate. Una durata
ancora sconosciuta richiede il probing gia limitato dal deadline del player.
Il percorso live rimane privo di durata/seek artificiale.

Verificati header e seek bidirezionale su MP4 faststart, moov finale, frammentati,
due audio/due sottotitoli e MKV. Questo riguarda i codec gia supportati dall'app:
non e l'aggiunta di un decoder HEVC/AV1 solo perche il contenitore e MP4.

## Sottotitoli dopo il seek

Due regressioni riprodotte: cursore invalidato da una lettura interrotta e indici
MOV/Matroska conservati dopo un seek che dichiara successo ma perde le cue.
Ogni richiesta di nuova posizione/selezione ora apre un cursore sottotitoli nuovo
in background sul worker gia riservato. Non ricrea il thread, non modifica la
traccia scelta e non attende questa operazione nel rendering video. Un hop a zero
non necessario viene evitato: il nuovo cursore e gia prima della prima cue.

L'adattatore riapre i sottotitoli alla posizione effettivamente raggiunta dal
decoder, includendo il clamp vicino alla fine. Una richiesta fallita non cambia
la selezione. Il costo di una nuova connessione/lettura header delle sorgenti
remote rimane asincrono e richiede una verifica della latenza delle cue su Vita.

## Identita dei test e diagnostica

CMake accetta `VITAMEDIADECK_TEST_ID` (1..32 caratteri ASCII, lettere/numeri/_/-).
In assenza di un'etichetta, i profili clock, planare e thread generano un ID.
Lo stesso identificativo compare nel titolo SFO della bolla, in Informazioni,
nella prima riga del log e nel nome `session_<id>.txt`. Gli ID applicazione
restano uguali: si installa una build alla volta, conservando le preferenze.

Il logger aggiunge una sola newline quando manca, anche con troncamento, cosi
record di produttori diversi non si concatenano. Il parser gestisce anche i log
precedenti: riconosce piu eventi sulla stessa riga senza scambiare `shown=128`
del riepilogo per FPS, o il `gap_max` audio per un intervallo video. Le frequenze
richieste sono registrate accanto a quelle riportate dalle API. Non sono misure
elettriche, di tensione o di autonomia.

## Riscontro nel log recuperato

Il SELF installato coincide byte per byte con la variante planare consegnata
prima di questo intervento. Il log superstite mostra solo aperture software P3
con sottotitoli disabilitati: non contiene la riproduzione fisica del difetto
segnalato sui sottotitoli. Riporta CPU 333, BUS/GPU 222, XBAR 111 MHz. Il suo
binario richiedeva il profilo a 444 MHz: la causa della differenza tra richiesta
e API non e attribuibile con certezza a un plugin da questo solo log.

Un intervallo 720p30 riporta 11,4 fps, lag 2575 ms e progresso media/wall 0,487x;
il sample 360p30 riporta 29,9 fps. Sono finestre della sessione recuperata, non
benchmark tra P2/P3 o misure di batteria. Non e corretto attribuire il divario al
solo layout planare senza lo stesso file e gli stessi clock in entrambe le prove.
La distribuzione consigliata mantiene il profilo performance e NV12/P2; 333 MHz
e P3 restano esperimenti con titoli e log distinti.

## Temi e verifiche

Scelta utente: Legacy, Ghost Mesh, Deck 90 e Signal conservati. Aperture, Relay e
Mosaic riprogettati: [piano e composizioni](themes-player-feedback-2026-10-23.md).
Le PNG host di home, browser e audio dei tre temi conservati sono identiche alla
consegna precedente. I test coprono hitbox anche durante l'apertura, mini-player,
riduzione del movimento, radio/podcast/back, riordino e comandi delle nuove viste.

Le suite FFmpeg/sanitizer, il logger concorrente e i controlli geometrici sono
prove locali. Build e audit VPK non attestano frame time, colori GXM, latenza
remota dei sottotitoli o riuscita della riproduzione fisica. Le prove richieste
sono nel file `PROVE-PLAYERFIX.txt` distribuito con i pacchetti.

# Verifica dei test PlayerFix sulla console — 23 ottobre 2026

L'utente ha completato le prove e conferma che i difetti visibili precedentemente
segnalati non si presentano più. Questo rapporto aggiunge la verifica dei tre log
recuperati dalla console, senza equipararla a una misura elettrica o a un benchmark
sistematico di tutti i temi e dei file supportati.

## Identità e integrità

Recuperati e preservati separatamente `session_playerfix-1023.txt`,
`session_stock333-1023.txt` e `session_planar-sw-1023.txt`, con dimensioni,
timestamp e SHA-256. Gli ID interni coincidono con quelli dei nomi dei log.
I tre VPK locali e le copie in Downloads della console coincidono con i rispettivi
SHA-256 del manifest di consegna. Il SELF attualmente installato coincide con
`VitaMediaDeck-Planar-Fix.vpk`. Tutti i 330 file dello snapshot sorgente conservato
alla consegna sono ancora identici. I test del parser dei log sono passati.

I log originali rimangono nella copia privata
`/private/tmp/vmd-console-review-20261023/`; il riepilogo riproducibile e privo di
URL, nomi dei video personali e credenziali è in
`../build/playerfix-20261023/console-validation/summary.json`.

## Bug segnalati e riscontri

| Verifica | Normale P2 | Stock P2 | Planare P3 |
| --- | ---: | ---: | ---: |
| Avvii/chiusure del worker video | 27/27 | 25/25 | 15/15 |
| Seek completati con `ret=0` | 17 | 14 | 8 |
| Riaperture sottotitoli applicate con `ret=0` | 18 | 2 | 2 |
| Segmenti audio con diagnostica di cadenza | 30 | 25 | 16 |
| Segmenti audio con `delayed` non zero | 0 | 0 | 0 |
| Errori del worker video alla chiusura | 0 | 0 | 0 |
| Aperture del player fallite per timeout | 0 | 2 | 0 |

Le 67 coppie riguardano worker riavviati anche da seek/cambi di decoder, non 67
video distinti. Le 22 applicazioni dei sottotitoli includono selezioni e
riposizionamenti: tutte le richieste registrate sono riuscite. Il riscontro
visivo dell'utente completa questa evidenza; il solo `ret=0` non prova il disegno
di ciascuna cue. I test funzionali della build normale includono MP4 e MKV con
entrambi i decoder. Un seek vicino alla fine del test da 24 secondi viene
correttamente limitato a 23.750 ms, con margine di 250 ms.

Tempi dei seek riusciti, comprensivi di riavvio e primo frame:

| Build | Minimo | Mediana | Massimo |
| --- | ---: | ---: | ---: |
| Normale, clip funzionali 320×180 a 10 fps | 118 ms | 181 ms | 221 ms |
| Stock, video personali | 256 ms | 421 ms | 641 ms |
| Planare, clip funzionali e video personali | 118 ms | 470,5 ms | 3.190 ms |

Le righe non sono un confronto P2/P3: contenuti, punti di seek e decoder variano.
Il seek planare da 3,19 secondi è nel 360p30: il demux audio richiede 15 ms e
l'attesa del primo frame 3.078 ms. Il costo risulta a valle del salto di indice,
compatibile con preroll/decodifica; non con una scansione audio lunga.

## CPU a 333 MHz e decoder hardware

In tutti i 22 eventi di clock le API riportano **CPU/BUS/GPU/XBAR
333/222/222/111 MHz**. La build normale e la planare chiedono 444/222/222/111;
la stock chiede 333/166/166/111. Le chiamate di impostazione interessate riportano
successo, ma il readback non cambia. Il motivo non è identificabile da questi
log e non viene attribuito a un plugin specifico. Non è una misura indipendente
del clock fisico. Il controllo del codice conferma le richieste e il rispetto
della sorgente di clock scelta nelle impostazioni.

Finestre di cadenza registrate, non medie dell'intero video:

| Decoder e contenuto | FPS nominali | FPS mostrati | Ritardo video | Media/wall |
| --- | ---: | ---: | ---: | ---: |
| Hardware, 640×360 | 30 | 29,9 | 0 ms | 0,999× |
| Hardware, 1280×720 | 30 | 29,8 | 8 ms | 0,994× |
| Software P2, 960×544 | 24 | 23,9 | 13 ms | 1,000× |
| Software P3, 960×544, cinque finestre | 24 | 23,9 | 9–14 ms | 0,999–1,000× |
| Software P3, 1280×720 | 30 | 11,4 | 2.591 ms | 0,487× |

Il decoder hardware funziona bene a CPU 333 MHz **per il 720p30 verificato**, con
bus e GPU a 222 MHz secondo le API e con il riscontro dell'utente. Non è ancora
una verifica del profilo completo 333/166/166, del 720p60 o di ogni bitrate/profilo
H.264. Nei campioni hardware 544p24 compaiono anche finestre da 22,3 a 23,9 fps;
non si dichiara quindi assenza assoluta di variazioni di cadenza.

## Due timeout software e ordinamento dei frame

Nella build stock, due passaggi al software su 720p30 partono dalle posizioni
5.064 e 4.761 ms. Entro il limite di circa 6 secondi il worker ha decodificato
83 frame, tutti scartati dal preroll, senza ancora pubblicare il frame richiesto.
Non risultano errori interni del codec o attese sulle superfici. Il comportamento
è compatibile con software sotto tempo reale e distanza dal keyframe precedente:
non indica un worker fermo. Il log `anchor=0` identifica l'indice della traccia
video, **non** la posizione temporale del keyframe.

Ogni apertura termina con `ret=-116`; la successiva riapertura hardware riesce
alla medesima posizione in circa 2,1–2,2 secondi. Il recupero esiste, ma aggiunge
circa 8,4–8,5 secondi complessivi fra tentativo software e ritorno all'hardware.
È una limitazione residua del passaggio al software su questo carico a 333 MHz,
non una nuova ricomparsa del difetto MP4 o dei sottotitoli. Aumentare soltanto il
timeout non renderebbe la riproduzione software 720p30 fluida.

I contatori `nonmono` hardware, 61 nella build normale e 262 nella stock,
sono coerenti con il riordinamento dei B-frame conservato dal codice. Nessun
campione richiede riparazioni PTS, timestamp mancanti o scarti tardivi del worker:
non vengono classificati come corruzione del video. Gli scarti totali includono
il preroll e non rappresentano da soli frame persi durante visione normale.

## Esito

I difetti segnalati si considerano risolti per le prove eseguite, sulla base della
conferma dell'utente e dei riscontri nei log. Il decoder hardware a CPU 333 MHz è
una configurazione valida per il 720p30 verificato. Il software 720p30 rimane un
limite documentato a questa frequenza; il planare non ha ancora un vantaggio
comparativo dimostrato. Si mantiene la versione normale NV12/P2 come riferimento,
senza cambiare i clock di default o ricompilare in assenza di una nuova correzione.

Per eventuali misure successive servono gli stessi file, gli stessi intervalli,
backend e clock effettivi controllati. Nessuna conclusione sul risparmio di
batteria deriva da questi log; la conferma dei temi è quella dell'utente, non
un profiling GPU della loro animazione.

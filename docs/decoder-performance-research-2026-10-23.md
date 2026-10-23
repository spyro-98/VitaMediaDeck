# Decoder Vita: verifica, correzioni e ricerca energetica

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

Ricerca e controllo dei sorgenti: **23 ottobre 2026**. Obiettivo: ridurre lavoro,
memoria e attese evitabili mantenendo riproduzione, sincronizzazione e qualità,
e predisporre confronti a frequenze inferiori. Nessuna misura di autonomia o
potenza della PS Vita è stata effettuata su Mac.

## Risultato dell'audit dei nostri componenti

L'app collega `vita-hw-decoder` e `vita-sw-decoder` allo **stesso FFmpeg n6.0**
modificato per Vita. Lo script standalone del pacchetto software menziona 8.1.2,
ma CMake dell'app forza entrambi alla libreria n6.0: quella è la dipendenza che
finisce effettivamente nel VPK. Mischiare le due ABI non è un aggiornamento valido.

Il percorso hardware usa già `h264_vita`, SceVideodec, output NV12 in CDRAM e GXM:
non effettua normalmente conversione RGBA o copia completa del frame sulla CPU.
Il software usa il decoder H.264 di libavcodec, tre thread per file seekable e uno
per ingressi forward-only, poi copia YUV420 in un anello separato per GXM. La
limitazione a un thread dei flussi forward-only resta: il codice documenta un
precedente crash Vita nell'avvio dei worker pthread e non è stata rimossa alla cieca.

La libreria ARM presente nel progetto contiene effettivamente le routine NEON
H.264 per IDCT, filtri e chroma: “abilitare NEON” da zero non è un miglioramento
ancora da fare. Il loro dispatch è verificabile nel
[sorgente ARM ufficiale di FFmpeg](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/arm/h264dsp_init_arm.c).

Il profilo gestito dall'app era ed è, nel pacchetto normale,
**CPU/BUS/GPU/XBAR 444/222/222/111 MHz**, indipendentemente da risoluzione e FPS.
La proprietà dei clock rimane una scelta esplicita: con PSVshell selezionato l'app
non li modifica. Questo audit non conclude che 444 MHz siano necessari per ogni file.

## Correzioni implementate

### Copia YUV senza scritture ridondanti

Con larghezze non coincidenti con il pitch, il percorso precedente azzerava
l'intero frame e poi ne riscriveva i pixel visibili. Adesso copia ciascun pixel una
volta e inizializza soltanto padding destro e righe nascoste. L'interlacciamento
UV NEON del percorso normale rimane. Sono validati puntatori, stride sufficienti,
stride negativi, dimensioni dispari, ordine U/V e limiti delle destinazioni.

Esempio analitico, non misura del bus: 854×480, pitch 864. Il precedente percorso
richiedeva 1.236.960 byte di scritture logiche di destinazione per frame; il nuovo
622.080. Sono eliminate 614.880 scritture di byte, circa il 49,7% **di quello
stadio di copia**, non della decodifica intera né del consumo della console. Il
percorso 1280×720 già perfettamente allineato non aveva quel medesimo spreco.

### Anello copiato solo quando serve

Il percorso hardware diretto riservava sia le venti superfici NV12 sia le otto
superfici indipendenti del fallback copiato. Ora l'anello copiato viene allocato
prima di aprire un codec che ne ha realmente bisogno. Un pool diretto inizializzato
solo in parte viene rilasciato prima del percorso alternativo.

A 1280×720 ogni allocazione viene arrotondata a 1,5 MiB: il percorso diretto passa
per questi buffer da 42 a **30 MiB**, evitando 12 MiB di CDRAM inutilizzata. Non
cambiano profondità della coda, riferimenti H.264, B-frame o dimensioni del pool
diretto. Il vantaggio dichiarato riguarda quella riserva di memoria, non tutti i
buffer interni del codec o il consumo elettrico.

### Attese tramite eventi kernel

Quando coda o superfici erano esaurite il worker riprovava ogni millisecondo.
Adesso attende un evento notificato al rilascio dei frame, incluso quello dopo il
fence GPU. La cancellazione resta osservabile tramite timeout massimo di 16 ms;
lo stop del join notifica subito. Se l'evento non è disponibile o fallisce, si
mantiene il polling precedente. Non vengono introdotte condition variable
pthread su thread Vita creati senza il loro runtime.

Il bit viene cancellato **prima** del controllo della condizione, evitando la
notifica persa fra controllo e attesa. La semantica del mask AND è verificata
anche nell'[implementazione di Vita3K](https://github.com/Vita3K/Vita3K/blob/master/vita3k/kernel/src/sync_primitives.cpp);
le API e i timeout sono quelli degli
[event flag VitaSDK](https://docs.vitasdk.org/eventflag_8h.html).

### Lifetime, diagnostica e allineamento

La diagnostica d'avvio non dereferenzia più `AVCodecContext` dal thread principale:
un clip breve può portare il worker a EOF e liberare quel contesto prima della
stampa. Numero e modalità dei thread sono conservati come valori indipendenti.
I contatori a 64 bit di copia e lavoro vengono aggiornati sotto lo stesso lock
usato dalle letture, evitando letture parziali su ARM a 32 bit.

Il nuovo `work=` distingue il lavoro attivo dall'aggregato storico `decode=`,
che comprende anche copia e attese sulla coda. È una misura del lavoro del worker
al netto di quelle attese, non un misuratore di energia e neppure il solo tempo
trascorso dentro istruzioni del codec.

Il pool hardware rispetta ora anche il minimo di 64 righe/colonne previsto dal
backend, importante per clip piccoli: il piano UV non deve sovrapporsi alle righe
che il motore scrive. Il contratto è documentato nel
[README tecnico di FFmpeg-vita](https://raw.githubusercontent.com/fish47/FFmpeg-vita/vita/README-vita.md).

Nel player il budget di 16.667 µs comprende rendering e sincronizzazione già
trascorsi; non viene aggiunto automaticamente un ulteriore millisecondo dopo lo
swap. I fence dei frame e il percorso di seek restano presenti.

## Tre varianti per confronti controllati

| Variante | Clock CPU/BUS/GPU/XBAR | Copia software | Scopo |
| --- | --- | --- | --- |
| Normale ottimizzata | 444/222/222/111 | NV12/P2, UV NEON | Confrontare le correzioni col VPK precedente alle stesse frequenze |
| Standard sperimentale | 333/166/166/111 | Identica alla normale | Verificare la cadenza senza il profilo di clock elevato |
| Planare sperimentale | 444/222/222/111 | YUV420P3, piani separati | Isolare il costo dell'interlacciamento UV nel software |

Tutte conservano i temi e le funzionalità dell'app. Hanno lo stesso ID applicazione:
si installa una variante alla volta. Per confrontare questi profili bisogna
selezionare la gestione clock dell'app; PSVshell continua deliberatamente a
prevalere quando è la sorgente scelta. Le frequenze effettive, non solo quelle
richieste, sono scritte nei log. Il profilo standard è sperimentale: la parità di
fluidità a 333 MHz non è provata dalla build.

L'upload planare è **disattivato di default** e riguarda il pacchetto software.
Copia Y, U e V in tre piani contigui con pitch a 32 byte e usa la conversione YUV
nativa di GXM. Mantiene l'allocatore FFmpeg ordinario e l'anello indipendente:
non riattiva il precedente esperimento CPU zero-copy, documentato come instabile.
L'hardware diretto rimane NV12 in tutte e tre le varianti. Sul dispositivo vanno
confrontati colori, crop, padding, FPS, tempo di copia e consumi: un minor lavoro
CPU può essere compensato da un diverso costo delle letture GPU.

Opzioni riproducibili di compilazione:

```sh
# Nell'app
-DVITAMEDIADECK_VIDEO_CLOCK_PROFILE=performance  # default
-DVITAMEDIADECK_VIDEO_CLOCK_PROFILE=stock

# Nei pacchetti: file seekable; forward-only resta single-thread
-DVITA_HW_DECODER_CPU_THREADS=1  # oppure 2 o 3, default 3
-DVITA_SW_DECODER_CPU_THREADS=1  # oppure 2 o 3, default 3

# Solo esperimento software; non attivo nella distribuzione normale
-DVITA_SW_DECODER_PLANAR_UPLOAD=ON
```

Le chiamate userland disponibili controllano le frequenze. Non abbiamo introdotto
una curva di tensione o un governor DVFS: abbassare MHz e risparmiare energia
mantenendo tutte le scadenze sono due verifiche distinte.

## Ricerca upstream: cosa è nuovo e cosa è già integrato

### FFmpeg-vita, wiliwili e mpv-vita

Sono stati confrontati i sorgenti attuali con quelli distribuiti nel progetto.
Al 23 ottobre, la patch
[wiliwili/FFmpeg per Vita](https://raw.githubusercontent.com/xfangfang/wiliwili/yoga/scripts/psv/ffmpeg/ffmpeg.patch)
ha lo stesso SHA-256 `2d38529d10c74560db3909cf8c2f3e359b128c04fb29f6dd0085770ed81cef6c`
della patch già integrata. Il repository principale è attivo, ma ciò non rende
nuovo il backend Vita contenuto nella patch.

Il fork [FFmpeg-vita di fish47](https://github.com/fish47/FFmpeg-vita) documenta
output diretti YUV/NV12 e dichiara anche 1080p; il nostro limite di pixel è
conservativo. Togliere quel limite non è un'ottimizzazione e richiederebbe nuove
prove di memoria, DPB, frame rate e compatibilità. I metadati GitHub consultati
riportano push del fork nel maggio 2024, non un nuovo decoder Vita del 2026.

Anche [mpv-vita](https://github.com/fish47/mpv-vita) è un riferimento utile per
presentazione e lifetime, non una prova che sostituire l'intero player migliori
consumi o seek. Il codice
[vitadec_video](https://raw.githubusercontent.com/fish47/FFmpeg-vita/vita/libavcodec/vitadec_video.c)
confronta CDRAM e memoria fisica contigua in un suo esperimento limitato: quei
numeri non sono misure del nostro player, e la disponibilità di memoria va
ricontrollata sul dispositivo.

### Letteratura: risultati applicabili e limiti

| Fonte primaria | Indicazione utile | Applicazione alla Vita |
| --- | --- | --- |
| [Benmoussa et al., EWiLi 2014, confronto su Cortex-A9](https://ceur-ws.org/Vol-1291/ewili14_6.pdf) | Hardware e software parallelo hanno costi energetici diversi; serve confrontare cadenza e potenza insieme | È architetturalmente pertinente, ma SoC i.MX6 e unità video non sono quelli della Vita. Nessun suo fattore di risparmio viene trasferito alla nostra console |
| [Ma, Hu, Wang, TMM 2011, complessità H.264 e DVFS](https://vision.nju.edu.cn/_upload/article/files/2f/7d/c03566ee4290a9f6d4e06dbc7f9f/3ee8fde0-7bab-4208-bd3a-7ba8b2ce9da2.pdf) | Stimare il lavoro dei moduli può guidare le frequenze richieste | Un eventuale controller va calibrato su tempi attivi e scadenze Vita, con isteresi; non sui tempi di attesa della coda |
| [Herglotz e Kaup, ICIP 2017, archivio arXiv 2023](https://arxiv.org/abs/2307.14000) | Istruzioni e cache miss possono stimare l'energia dopo calibrazione | Su Vita non abbiamo validato un accesso userland equivalente ai PMU del paper; i soli millisecondi CPU non danno mWh |
| [Kränzler et al., PCS 2024, confronto software/hardware](https://arxiv.org/abs/2402.09001) | Implementazione ottimizzata, SIMD e motore hardware contano oltre al codec | Conferma di misurare il percorso completo; non dimostra che AV1 o HEVC siano più efficienti sulla Vita |
| [Kränzler et al., modello energia hardware, rev. dicembre 2024](https://arxiv.org/abs/2402.09926) | Il profiling software aiuta modelli di energia hardware se addestrati su dati misurati | Potenziale strumento di ricerca; non è un sostituto delle misure elettriche del dispositivo |

Le date dei paper sono quelle di pubblicazione/revisione, non la data recente
con cui un motore di ricerca ha riscoperto un PDF. Le cifre energetiche pubblicate
su altre piattaforme non sono stime della nostra autonomia.

### Decoder alternativi e aggiornamenti

**OpenH264.** Supporta ARMv7/NEON ed è un candidato per un benchmark separato. La
[README](https://raw.githubusercontent.com/cisco/openh264/master/README.md) è meno
aggiornata del
[registro delle release](https://github.com/cisco/openh264/blob/master/RELEASES):
quest'ultimo documenta B-frame Main/High dalla 2.0 e successive correzioni del
multithreading. Non è quindi corretto scartarlo dicendo soltanto “supporta Baseline”.
Prima di integrarlo vanno provati profili, B-frame, seek, errori e inizializzazione
dei thread; esiste anche una
[segnalazione riproducibile del 2026 sul cambio thread dopo Initialize](https://github.com/cisco/openh264/issues/3969).
È una segnalazione upstream da verificare sulla revisione scelta, non una prova
che ogni versione sia difettosa. Non viene sostituito il decoder attuale senza
un confronto sul Cortex-A9.

**dav1d 1.5.4.** Il
[build system corrente](https://raw.githubusercontent.com/videolan/dav1d/master/meson.build)
e il [progetto ufficiale](https://github.com/videolan/dav1d) includono ARM a 32 bit:
è una possibilità concreta per un futuro port AV1, inizialmente a risoluzione
ridotta. È un decoder AV1, non un acceleratore H.264; non rende più rapido il
percorso già funzionante e non dimostra 720p AV1 sul nostro hardware.

**FFmpeg recente.** La [pagina ufficiale delle release](https://ffmpeg.org/download.html)
riporta al controllo 9.0.1 e 8.1.2 sui rispettivi rami. L'app usa n6.0 perché il
backend Vita e gli adattatori sono integrati lì. Una migrazione deve portare
insieme demux, codec, API private Vita, patch custom IO e test; non basta collegare
la libreria software nuova accanto alla vecchia. Priorità agli interventi ARMv7
reali. Le ottimizzazioni AArch64, SVE o AVX non si eseguono sul Cortex-A9. Il
[supporto ufficiale ARM del Cortex-A9](https://support.arm.com/compute-ip/cortex-a9)
identifica infatti l'ISA Armv7-A.

## Piano degli esperimenti, in ordine di utilità

| Priorità | Soluzione e punto d'integrazione | Prova richiesta / criterio |
| --- | --- | --- |
| P0, implementata | Padding, anello copiato lazy, eventi e lifetime nei due `software_video.c` | Bit-identicità e ownership host; regressioni seek/stop e latenza su Vita |
| P0, predisposta | Profilo standard del clock guard, stesso resto della pipeline | Normale vs standard a parità di file/tema/luminosità: FPS, drift A/V, underrun e batteria |
| P1, prototipo pronto | Upload YUV420P3 nel software, senza CPU zero-copy | Normale vs planare agli stessi clock; colori identici e costo GPU/CPU totale inferiore |
| P1, predisposta | Uno/due/tre frame thread in `open_h264_decoder` | Stessi CRC e deadline su Vita; non scegliere solo il massimo throughput su Mac |
| P1, ricerca | Demux multiplexato unico con due code di pacchetti, invece di due cursori che rileggono/scartano tracce | Misurare I/O locale/rete e seek; preservare abort, indipendenza audio e sostituzione traccia. Richiede un nuovo broker, non la condivisione non protetta di AVFormatContext |
| P1, ricerca | Governor con livelli discreti, margine sulle deadline e isteresi | Usa `work`, coda e lag, non solo FPS dichiarati. Sali subito sul rischio di underrun, scendi dopo intervallo stabile. Calibrazione sul dispositivo prima del default |
| P2 | Slice threading solo con bitstream effettivamente multislice | Servono fixture mono/multislice e un profilo esplicito; il decoder non crea slice indipendenti da un file che non le ha |
| P2 | Pool software GPU-visibile con AVBufferPool e get_buffer2 thread-safe | Nuovo backend isolato, FATE/CRC, pressione DPB e fence. Non riattivare il vecchio zero-copy instabile |
| P2 | Presentazione solo ai cambi effettivi del frame quando HUD è nascosto | Conservare risposta input, VSync, sottotitoli e fence; misurare duplicati e costo GPU, anche con MSAA/temi |
| P2 | Port del backend Vita su FFmpeg recente e benchmark OpenH264 | Nuova dipendenza/pipeline in parallelo; test su A9, non numeri presi da ARM64 |
| P3 | AV1/dav1d o altri codec software | Compatibilità aggiuntiva, non promessa di maggiore autonomia per H.264 |
| P3 | Allargare limiti a 1080p o aggiungere MPEG-4 Part 2 via API dedicate | Nuove capacità; richiedono memoria e test propri, non aumentano automaticamente l'efficienza AVC |

La documentazione FFmpeg avverte che il
[frame threading aggiunge ritardo per i thread oltre il primo](https://ffmpeg.org/doxygen/trunk/multithreading_8txt.html).
Il nostro ring copiato evita inoltre che il lifetime delle superfici GPU diventi
quello dei buffer di riferimento interni di libavcodec.

Sono valutabili anche ricodifiche/server representation H.264 8-bit adeguate allo
schermo, meno riferimenti, GOP più breve o più slice. Devono essere confrontate
alla stessa qualità: meno lavoro di decodifica può aumentare bitrate, I/O e costo
Wi-Fi. Saltare deblocking o frame è un compromesso di qualità/cadenza, non una
ottimizzazione lossless. I compromessi high-FPS già presenti non sono stati
ampliati durante questo intervento.

Non è stato trovato un backend Vita pubblico già pronto che trasformi i motori
esistenti in un decoder hardware HEVC/AV1. Il supporto SDK verificabile è quello
negli [header Videodec](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/videodec.h).
GPU compute generico, algoritmi neuronali o patch per processori moderni non sono
scorciatoie dimostrate per questo chip.

## Verifiche eseguite e come ripeterle

Le suite includono i veri sorgenti dei wrapper con FFmpeg 6 e NEON su Apple Silicon,
sostituendo solamente CDRAM/GXM/eventi con un modello host. ASan/UBSan coprono
formati allineati e non, stride negativi, errore di allocazione a metà pool,
conversione normale/planare, clone AVFrame e rilascio dopo fence, stop e polling
alternativo. ThreadSanitizer controlla separatamente le nuove attese, senza
spacciare quel test per una prova completa delle librerie su Vita.

```sh
VMD_HOST_FFMPEG=/percorso/ffmpeg6 bash tests/decoder/run-host-tests.sh
VMD_HOST_FFMPEG=/percorso/ffmpeg6 bash tests/decoder/run-tsan.sh
VMD_HOST_FFMPEG=/percorso/ffmpeg6 bash tests/decoder/run-benchmarks.sh
python3 tests/decoder/log_test.py
python3 tools/analyze-decoder-log.py session.log > decoder.json
```

Il benchmark genera clip sintetici H.264 con B-frame e riferimenti, decodifica
360p25, 480p30, 720p30 e 720p60 con uno/due/tre thread, tre ripetizioni, e confronta
CRC dei pixel in ordine d'uscita. Un esempio misurato **sul Mac**, per 60 frame
854×480: mediane 53,282/29,848/30,781 ms di tempo trascorso e
53,031/56,756/77,606 ms di CPU, rispettivamente. Tutte le configurazioni producono
lo stesso CRC. Questi dati dimostrano la riproducibilità del confronto e il fatto
che parallelismo e lavoro totale non coincidono; non selezionano il numero ideale
di thread per Vita e non misurano energia.

Le prove fisiche necessarie sono in `PROVE-DECODER-SU-VITA.txt` nel pacchetto.
Per misurare autonomia: console scollegata dall'USB, stessa luminosità, audio,
Wi-Fi, clip, tema e stato termico; ripetizioni alternando le varianti. Percentuali
di batteria per pochi minuti, FPS isolati o tempi CPU non bastano a dichiarare
una percentuale affidabile di risparmio. Un misuratore esterno deve anche tenere
conto dell'eventuale ricarica della batteria durante la prova.

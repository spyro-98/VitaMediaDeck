# Regressioni di riproduzione dopo le prove Vita

I test includono i veri `media_tracks.c` e `vita_decoder.c` e i due header
`input_timing.h` usati dai pacchetti. I worker SceKernel sono sostituiti con
pthread host; FFmpeg e la versione 6 fissata dal progetto. Nessuna prova esegue
il motore hardware o GXM del dispositivo.

```sh
VMD_HOST_FFMPEG=/percorso/ffmpeg6 bash tests/playback_seek/run-host-tests.sh
python3 tests/decoder/log_test.py
```

Servono clang, FFmpeg CLI con libx264/AAC/MOV_TEXT, Python 3 e librerie statiche
FFmpeg 6 host (avformat, avcodec, avutil). Lo script e per macOS; i framework di
sistema sono necessari per le librerie FFmpeg del banco di prova.

Copertura: MP4 con moov finale e iniziale, frammentati, multitraccia, MKV;
durata dal solo header e indici per seek avanti/indietro; origine non nulla,
durata assente e metadati limite; selezione di due tracce sottotitoli e lingue;
seek avanti/indietro/EOF e cancellazione di una lettura che invalida il cursore.
Il test dell'adattatore controlla la posizione raggiunta dal backend, incluso
il clamp finale, e preserva traccia selezionata, Off e comportamento d'errore.
Il logger reale e verificato con righe troncate e quattro produttori concorrenti.

Il FFmpeg 6 host disponibile non abilita il demuxer SRT esterno: le cue SRT sono
muxate come SubRip in MKV e MOV_TEXT in MP4. I sottotitoli esterni e ASS vanno
provati sulla console con la build nativa, senza dedurne il supporto dal test.

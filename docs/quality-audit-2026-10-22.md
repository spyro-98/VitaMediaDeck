# Verifica qualità e prove su Vita — 22 ottobre 2026

## Stato

Baseline locale 1.3.0 sul ramo `feature/iptv`. La revisione conserva il lavoro
locale preesistente e non pubblica una release. Il pacchetto da provare è
`build/quality-20261022/VitaMediaDeck.vpk`; nello stesso percorso sono presenti
i simboli del relativo eseguibile, i log delle verifiche e gli hash SHA-256.

## Correzioni

- **Annullamento tra thread:** i due callback di `vita-https` e il controllo
  del genitore nel prefetch audio usano letture atomiche del flag. Una prova
  sui callback reali riproduce la race precedente con ThreadSanitizer e passa
  dopo la modifica. Il contratto dei flag condivisi è documentato nell'header;
  la forma delle API non cambia.
- **Copertine podcast:** un errore durante la lettura dell'intestazione viene
  respinto subito; il ciclo di copia non ripete letture dopo EOF o dopo un
  errore. La pubblicazione resta subordinata al completamento della copia.
- **Verifiche ripetibili:** corretto l'include mancante nella suite Crossfade,
  aggiornato il controllo del catalogo ai gutter correnti e aggiunto un runner
  audio/cache/prefetch. Le fixture di persistenza vivono in directory temporanee.
- **Documentazione:** aggiornati i limiti HLS e lo stato delle prove DASH;
  corretti i riferimenti alle 720 permutazioni dei sei tab correnti.

## Verifiche eseguite sul Mac

| Area | Risultato |
| --- | --- |
| Analisi statica | Otto sorgenti di trasporto, manifest, cache e metadati senza segnalazioni nella verifica finale. |
| Annullamento HTTPS | ASan/UBSan e ThreadSanitizer: callback reali, flag nullo/set/unset e 200.000 letture concorrenti per esecuzione. |
| Audio online | 6.075 asserzioni per esecuzione; passaggi ASan/UBSan e ThreadSanitizer. |
| Prefetch | GET simulati da 800 ms; attesa massima a regime 1–4 ms nelle esecuzioni osservate. Seek durante un fetch, EOF, errori e annullamento passano. |
| Cache e salvataggi | Riutilizzo, corruzione, limiti di spazio/file, preferiti IPTV e cancellazione dei salvataggi. |
| Podcast offline | 140 controlli su metadati, copertine permanenti, rinomina/spostamento, collisioni e cancellazione. |
| Navigazione | 29 asserzioni sui caricamenti falliti/annullati; 16.563 controlli su tutte le 720 permutazioni dei tab. |
| UI/UX | Focus finito e Reduce motion; 1.703 controlli layout audio, 2.720 impostazioni/guida/azioni, 303 stati download, 3.205 UI IPTV e 9.362 catalogo. Render host ispezionati in italiano/inglese, anche con mini-player e nomi lunghi. |
| Artwork | 233.798 controlli; proporzioni conservate e confronto byte per byte delle 251 bandiere impacchettate. |
| IPTV | DNS/403/404, cancellazione, limiti del corpo, 3.286 asserzioni sorgente, 54 sessione; VOD sintetico da 300 segmenti senza richieste duplicate del manifest. |
| HLS | Nove varianti: TS, fMP4, CTS negativi, A/V separati, byte range, mappe, AES con IV implicito/esplicito e rotazione chiavi. Due lettori reali FFmpeg e prove lifecycle, ASan/UBSan e ThreadSanitizer. |
| DASH | Parsing e limiti, due lettori FFmpeg, shift di un'ora/PTO, refresh/end, cambi periodo, backpressure e annullamento; ASan/UBSan e ThreadSanitizer. |
| Rete reale | HLS Mux: sei secondi decodificati, 181 frame video e 131 audio; scarto iniziale 0 us nella prova. SomaFM MP3: 5.015 ms, 44,1 kHz stereo/128 kbps e metadati. Audio non emesso, media non registrati. |
| Build Vita | Release `-O3` effettivo per app, decoder, IPTV e HTTPS. VPK verificato con `unzip -t` e audit di backend TLS, provenienza e licenze. |

I render usano font e sostituti host: non sono schermate della console. Le
prove di rete usano curl del Mac; i decoder host non eseguono SceVideodec o
SceAudiodec. Questi risultati non misurano frame time, uscita audio, stack TLS
o stabilità prolungata della Vita. Il VOD sintetico viene elaborato dal test,
non riprodotto per trenta minuti in tempo reale.

Vita3K è stato provato separatamente: raggiunge il bootstrap e l'indicizzazione
locale, ma persistono errori nativi `EXC_BAD_ACCESS`; Home, input e riproduzione
non sono stati validati nell'emulatore. Il problema non è attribuito con certezza
all'app o all'emulatore e non è considerato risolto da questa revisione.

Per la build, al VitaSDK spostato sul disco secondario mancavano `cc1` e `lto1`.
Sono stati recuperati da un pacchetto ufficiale GCC 15.2.0 in un ambiente
temporaneo, usando il SDK e le librerie del progetto senza riscriverli.

## Checklist per la console

Usare il VPK in `build/quality-20261022`, conservando i dati dell'app. Per
gli errori abilitare i log e annotare sequenza, sorgente, decoder e tempo fino
al problema. Non condividere URL firmati, credenziali o file di password.

1. **Installazione e avvio a freddo.** Installare, chiudere completamente e
   riaprire due volte. Verificare Home, preferenze e tab conservati; annotare
   tempo d'installazione e tempo alla Home. Nessun crash o blocco.
2. **Video locale e server già configurati.** MP4/MKV compatibile per almeno
   15 minuti; seek vicino/lontano avanti e indietro, pausa/ripresa, tracce audio,
   sottotitoli e cambio HW/SW. Minimizzare e tornare a schermo intero: posizione,
   pausa e selezioni devono essere conservate. Ripetere un video remoto con
   i server di cui si dispone; annullare un'apertura lenta senza bloccare la UI.
3. **HLS e DASH.** Provare il campione Mux sotto e uno stream autorizzato già
   noto. Per HLS includere, se disponibili, fMP4, audio separato e AES-128;
   riprodurre almeno 15 minuti. Ripetere apertura/chiusura dieci volte e provare
   il decoder software/fallback che causava il crash. Audio/video devono restare
   sincronizzati e l'uscita deve restituire il controllo alla lista. Sul live la
   ripresa torna al punto live; il VOD finito riprende dalla pausa. Il seek dei
   flussi segmentati resta indisponibile.
4. **Radio e concorrenza.** Ascoltare una stazione MP3 per almeno 15 minuti,
   verificare metadati, pausa/ripresa e perdita/ritorno del Wi-Fi. Navigare e
   scaricare contemporaneamente un file: niente blocchi, crepitii sistematici o
   interruzioni dell'audio. Un errore remoto deve essere spiegato nell'interfaccia.
5. **Podcast online e offline.** Aprire anche un feed grande (es. Nova Lectio),
   scorrere più pagine, avviare un episodio, seek e mini-player. Scaricare un
   episodio, poi disattivare il Wi-Fi e riavviare: titolo/copertina/resume devono
   restare disponibili. Rinominarlo/spostarlo/eliminarlo dall'app senza toccare
   file estranei; verificare le azioni anche con un download annullato.
6. **Liste e filtro IPTV.** Salvare una lista, rinominarla, cercare, aggiungere e
   rimuovere preferiti. Eseguire Filtra errori e annullarlo a metà: solo DNS/403/404
   verificati possono sparire, gli esiti incerti restano visibili. Mostra tutti
   deve ripristinare ogni voce. Rimuovere un salvataggio non elimina i file o i
   canali salvati separatamente; la conferma parte su Annulla.
7. **UI, touch e accessibilità.** Con e senza mini-player controllare nomi lunghi,
   copertine quadrate, drawer L1/R1, tutti i tab, tre layout audio e guida. Provare
   touch e pulsanti: il focus deve corrispondere alla scelta e le aree touch al
   disegno. Con Reduce motion gli spostamenti sono immediati. Riaprire l'app per
   verificare la persistenza delle impostazioni.
8. **QR e ciclo della console.** Scansionare un QR con la fotocamera posteriore,
   annullare e ripetere un download, poi provare ECO, blocco input, sospensione e
   ripresa. Durante il blocco i pulsanti non devono cambiare preferiti o azioni;
   dopo la ripresa la UI deve rispondere. Verificare nuovamente audio e rete.

Campione HLS pubblico usato sul Mac:
`https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8`

## Perimetro del piano

EPG/XMLTV, DRM, registrazione/timeshift, coda di riproduzione e integrazione
YouTube sono funzionalità ulteriori; non sono state aggiunte né dichiarate
complete. La chiusura della preview attuale richiede le prove console sopra.

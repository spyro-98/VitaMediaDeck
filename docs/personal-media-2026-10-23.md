# Navidrome e Immich — 23 ottobre 2026

Le voci **L1 → Navidrome** e **L1 → Immich** sono disponibili con Legacy,
Ghost Mesh, Deck 90, Signal, Aperture, Relay e Mosaic. Riutilizzano il browser
condiviso, le griglie/liste, il player musicale e il visualizzatore immagini;
non cambiano i sei settori della Home o l'assegnazione degli stick.
I collegamenti sono presenti anche in **Home → Libreria**, **Audio locale →
Navidrome** e **Immagini → Immich**. Nel tema Legacy, L1 li raggruppa sotto
Contenuti e servizi: [riordino e nuova resa nativa](game-ui-2026-10-23.md).

## Configurazione

1. Aprire la sezione, premere **Quadrato** e inserire nome e URL completo del
   server, inclusi schema, porta ed eventuale percorso del reverse proxy.
   Esempi di forma, non server configurati: `https://music.example/navidrome`,
   `http://192.168.1.20:4533`, `https://photos.example`.
2. Navidrome richiede utente e password; Immich una chiave API personale.
   Per Immich `/api` viene aggiunto quando manca: è accettato anche un URL
   che termina già in `/api`.
3. **X** collega, **Triangolo** modifica. **R1**, seguito da un secondo R1,
   rimuove il collegamento locale. Non elimina contenuti sul server.
   **Cerchio** torna indietro. L1 e il logo aprono il menu sezioni.
4. Le credenziali sono solo in memoria per default. L'opzione esistente
   **Ricorda password di rete** abilita anche il salvataggio di password
   Navidrome e chiavi Immich nel file delle credenziali in chiaro. La banca
   dati dei server non contiene credenziali.

Per una chiave Immich con permessi selettivi servono `user.read`, `album.read`,
`asset.read` e `asset.view`. La chiave deve appartenere all'utente che può
leggere le raccolte desiderate. Un rifiuto 401/403 fa richiedere nuovamente
le credenziali. Un certificato privato passa dal dialogo esistente di conferma
esplicita dell'impronta; non si disabilita automaticamente la verifica TLS.
Usare l'URL finale del server: le nuove richieste autenticate non seguono redirect.

## Navidrome

- Autenticazione OpenSubsonic 1.16.1: `u`, `t=MD5(password+salt)`, `s`, `v`,
  `c`, `f=json`. Il salt di 128 bit proviene dal generatore della Vita e cambia
  a ogni nuova richiesta API. Nessun OAuth o accesso tramite pagina web.
- Verifica della connessione con `ping`; album alfabetici con `getAlbumList2`,
  artisti paginati tramite `search3`, album dell'artista con `getArtist`,
  playlist con `getPlaylists`/`getPlaylist`, tracce con `getAlbum`.
- Copertine tramite `getSong`/`getCoverArt`; streaming originale con
  `stream?format=raw`, HEAD e richieste Range. Le URL firmate restano nel
  trasporto: gli identificativi dei file, la cronologia e la cache non le salvano.
- MP3 usa mpg123; AAC/M4A il percorso audio AvPlayer; FLAC il decoder FLAC
  esistente. Per FLAC sono supportati mono/stereo, 4–32 bit, frequenze del
  player fino a 48 kHz. Opus/Vorbis e FLAC ad alta frequenza non vengono
  convertiti automaticamente dal server in questa integrazione.
- Pausa, seek, volume, ECO, blocco input, ripetizione e mini-player usano i
  comandi del player musicale. La coda sequenziale carica anche le pagine
  successive. Shuffle riguarda i brani della pagina attualmente in coda.
  Il ripristino dal mini-player conserva la coda e inoltra le scelte di sezione.

## Immich

- Autenticazione tramite header `x-api-key`; verifica con `GET /api/users/me`.
- Tutte le foto e preferiti tramite `POST /api/search/metadata`; si leggono
  sia `nextPage` sia `nextCursor`. Album con `GET /api/albums`, contenuti
  con `GET /api/albums/{id}`. Il percorso legacy delle ricerche è mantenuto
  per compatibilità con Immich 2.x e con i campi ancora accettati dallo schema 3.x.
- Le foto sono filtrate come `IMAGE`; i video non entrano nella raccolta foto.
- `GET /api/assets/{id}/thumbnail?size=preview` fornisce la rappresentazione
  elaborata dal server. Sono gestiti JPEG, PNG e WebP, anche quando l'originale
  è HEIC. Immich deve avere già generato le anteprime.
- La foto si apre nel visualizzatore esistente con zoom, pan e rotazioni di 90°.
  Il file temporaneo della foto viene rimosso quando si chiude il visualizzatore.
  I contenuti originali sul server non sono modificati.

## Limiti di memoria e rete

Pagine fino a 100 elementi; catalogo JSON fino a 4 MiB per risposta e preview
fino a 16 MiB. Le API album/playlist senza paginazione nativa vengono suddivise
localmente dopo il download della risposta: una singola risposta oltre 4 MiB
viene rifiutata, non troncata. Cache di streaming fino a 8 MiB con prefetch,
seek, cancellazione e chiusura del worker. Il decoder immagini mantiene i
limiti già presenti per le texture e scala WebP prima della conversione.
I timeout sono finiti; le immagini si possono annullare durante il caricamento.

Non sono implementati upload, eliminazione di foto/brani remoti, modifica
playlist, scrittura dei preferiti o scrobbling. I cataloghi e la fruizione sono
in lettura. La copia del VPK non equivale a installazione o collaudo sulla Vita.

## Verifiche automatiche

- `tests/personal_media/run-host-tests.sh`: token e salt, Unicode, ID/query
  non validi, errori 401/403, JSON malformato/duplicato, pagine e playlist grandi,
  metadati, corrispondenza delle chiavi ai server, HTTP reale con trasporto
  di produzione e due server locali, blocco dei redirect e limite delle risposte.
- Cursore di produzione con prefetch reale, letture attraverso più blocchi,
  seek avanti/indietro, telemetria, abort e join della chiusura.
- Rendering host delle schermate native nei sette temi, due lingue, con/senza
  mini-player, configurazione/modifica/annullamento, fallimento del salvataggio,
  inoltro al player e al visualizzatore, menu completo con player e download.
- Regressioni dei player e dei temi, ASan/UBSan, build Vita ottimizzata e audit VPK.

Le immagini nei risultati dei test sono rendering host del codice UI, non
screenshot o misurazioni di una PS Vita. I server di prova contengono dati
artificiali dichiarati; non sono stati usati account o librerie personali.

## Prove da eseguire su PS Vita con i server reali

1. Configurare entrambi gli URL reali, utente/password Navidrome e chiave Immich.
   Verificare anche riavvio con l'opzione di salvataggio credenziali attiva/disattiva.
2. Navidrome: album, artisti e playlist oltre una pagina; ascoltare MP3, M4A/AAC
   e FLAC compatibili. Provare seek avanti/indietro, pausa, cambio brano,
   passaggio alla pagina seguente della coda, ripetizione e shuffle.
3. Minimizzare la musica, aprire Immich, ripristinare il player con Start o dal
   mini-player; cambiare sezione dal player ripristinato. Controllare audio
   continuo, copertina, volume, blocco input ed ECO.
4. Immich: foto e album oltre una pagina, preferiti, originali HEIC con preview
   JPEG/WebP, orientamenti diversi; aprire/chiudere ripetutamente, zoom, pan e
   rotazione. Confrontare il consumo memoria e l'attesa con raccolte reali.
5. Provare chiave scaduta/revocata, permessi mancanti, server offline, connessione
   interrotta durante fetch/seek e certificato privato con impronta verificata.
6. Ripetere apertura/ritorno in tutti i temi e con un download attivo. Controllare
   bordi della selezione, touch e assenza di tab Legacy nei temi nuovi.

## Riferimenti ufficiali verificati

- [Compatibilità Subsonic di Navidrome](https://www.navidrome.org/docs/developers/subsonic-api/)
- [OpenSubsonic: autenticazione e parametri](https://opensubsonic.netlify.app/docs/api-reference/)
- [OpenSubsonic search3 e query vuote](https://opensubsonic.netlify.app/docs/endpoints/search3/)
- [Streaming Navidrome](https://github.com/navidrome/navidrome/blob/master/server/subsonic/stream.go)
- [Schema OpenAPI Immich](https://github.com/immich-app/immich/blob/main/open-api/immich-openapi-specs.json)
- [Documentazione API Immich](https://api.immich.app/)

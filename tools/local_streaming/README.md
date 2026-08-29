# Server locali per i test di VitaMediaDeck

I tre script espongono in sola lettura una cartella del Mac tramite gli stessi
protocolli supportati da VitaMediaDeck: WebDAV HTTPS, SFTP e SMB. Il server SMB di
prova negozia SMB 2.0.2, che esercita il backend SMB2/SMB3 dell'app. Mac e PS Vita
devono essere collegati alla stessa rete locale e il firewall di macOS deve
consentire le connessioni in ingresso a Python.

## Preparazione una tantum

Da Terminale:

```sh
cd /Users/eliaspiga/lavoro/personale/Vita/VitaMediaDeck
python3 -m venv .venv-local-streaming
.venv-local-streaming/bin/python -m pip install -r tools/local_streaming/requirements.txt
```

WebDAV usa soltanto la libreria standard di Python e `openssl`, gia' incluso in
macOS. Le due dipendenze installate nell'ambiente virtuale servono a SFTP e SMB.

## 1. WebDAV HTTPS

```sh
cd /Users/eliaspiga/lavoro/personale/Vita/VitaMediaDeck
python3 tools/local_streaming/webdav_server.py --media-dir /Users/eliaspiga/Movies
```

Lo script stampa IP, porta, credenziali e pin TLS. In VitaMediaDeck crea una fonte
WebDAV con:

- host: l'IP del Mac stampato dallo script;
- porta: `8443`;
- percorso iniziale: vuoto;
- utente/password: `vitamediadeck` / `vitamediadeck`.

Il certificato locale e' autofirmato. Al primo accesso VitaMediaDeck mostra il pin
`sha256//...`: confrontalo con quello stampato nel Terminale e confermalo solo
se coincide. Chiave e certificato vengono conservati nella cache utente, cosi'
il pin resta stabile tra un avvio e l'altro finche' gli indirizzi del Mac non
cambiano.

## 2. SFTP

```sh
cd /Users/eliaspiga/lavoro/personale/Vita/VitaMediaDeck
.venv-local-streaming/bin/python tools/local_streaming/sftp_server.py --media-dir /Users/eliaspiga/Movies
```

In VitaMediaDeck crea una fonte SFTP con host uguale all'IP stampato, porta `2222`,
percorso iniziale `/` e credenziali `vitamediadeck` / `vitamediadeck`. Al primo accesso
confronta la fingerprint SHA-256 mostrata dalla Vita con quella del Terminale.

## 3. SMB2

```sh
cd /Users/eliaspiga/lavoro/personale/Vita/VitaMediaDeck
.venv-local-streaming/bin/python tools/local_streaming/smb_server.py --media-dir /Users/eliaspiga/Movies
```

In VitaMediaDeck crea una fonte SMB con host uguale all'IP stampato, porta `1445`,
condivisione `VITAMEDIADECK`, percorso e dominio vuoti, credenziali
`vitamediadeck` / `vitamediadeck`.

Ogni server resta in primo piano e si arresta con `Ctrl-C`. Per usarli insieme,
aprili in tre finestre o pannelli Terminale distinti. Porta, credenziali,
cartella e indirizzo di ascolto possono essere cambiati; usa `--help` per
l'elenco completo delle opzioni. Metti nella cartella almeno un MP4/M4V/MOV/MKV
seekable con video H.264; l'audio AAC e' supportato ma opzionale. Per coprire il
contratto attuale dell'app, aggiungi anche un Matroska creato da
VitaMediaDeck-Transcoder con piu' tracce AAC, almeno due sottotitoli testuali e
un `cover.jpg` incorporato.

Per ogni protocollo verifica:

1. anteprima della copertina nella griglia e nella lista;
2. estrazione di un fotogramma quando la copertina non e' presente;
3. cambio delle tracce audio e dei sottotitoli dal pannello R1;
4. salvataggio del punto di arresto, ripresa dopo la riapertura e azione
   **Ricomincia dall'inizio**;
5. rifiuto di credenziali, fingerprint o pin errati senza scrivere password
   nella cache delle anteprime.

Questo e' il contratto di riproduzione remota attualmente supportato dall'app;
la compilazione locale non sostituisce la verifica su una PS Vita fisica.

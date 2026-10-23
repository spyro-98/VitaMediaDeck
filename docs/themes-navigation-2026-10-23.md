# Temi e navigazione — 23 ottobre 2026

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

## Correzioni

Il percorso radio/podcast riutilizzava il selettore compatto della Home e il browser di scoperta precedente. Inoltre un flag di presentazione veniva ripristinato all'uscita dalla Home locale. I temi nuovi mantenevano quindi la palette, ma perdevano la propria composizione entrando nel catalogo o tornando indietro.

Ora tutti i temi non Legacy usano la propria struttura anche per radio, podcast, scoperta, paesi, generi, episodi e preferiti. Tornare da un catalogo o dagli episodi conserva il tema. La barra scorrevole nell'header e la precedente coppia di tab radio/podcast appartengono a Legacy. I servizi, le azioni R1 e i player esistenti restano utilizzati: sono condivisi, ma non impongono più il layout precedente.

Il percorso dei podcast offline è una voce esplicita dell'elenco nei temi nuovi. In Legacy resta la colonna dedicata. Ingressi nel player e successivi ritorni sono coperti anche dai test del ciclo della schermata online, con il player sostituito sull'host.

## Ghost Mesh: due stick indipendenti

- **Stick destro:** seleziona direttamente una delle sei sezioni della ruota, secondo la direzione. La mappatura segue anche l'ordine personalizzato delle sezioni.
- **Stick sinistro o D-pad:** seleziona i contenuti del pannello sinistro. Raggiungere il primo elemento non trasferisce gli input alla ruota.
- **X:** apre il contenuto selezionato; R1 apre le opzioni; L1 apre il pannello generale.
- Un movimento mantenuto al passaggio locale → radio → podcast non viene riapplicato come una nuova scelta. Sono presenti zona neutra e isteresi per evitare oscillazioni ai confini fra settori.
- Pannelli modali e blocco del mini-player hanno precedenza su entrambi gli stick. Dopo un pannello, lo stick destro deve tornare neutro prima di una nuova selezione.

La UI mostra i suggerimenti dei due stick. La ripartizione riguarda la Home e i cataloghi dei temi; i comandi dei player rimangono quelli precedenti.

## Sette temi disponibili

Selezione e salvataggio: **Impostazioni → Interfaccia → Tema**, X passa alla scelta successiva.

| Tema | Struttura |
| --- | --- |
| Legacy | UI precedente e tab scorrevoli nell'header. Rimane il valore predefinito. |
| Ghost Mesh | Ruota delle sezioni sulla destra ed elenco indipendente sulla sinistra. |
| Deck 90 | Sezioni verticali, directory e font monospaziato. |
| Signal | Titolo grande, accento arancio, sezioni in alto e righe numerate. |
| Aperture | Anteprima ampia a sinistra, contenuti a destra e sezioni nella fascia inferiore. |
| Relay | Sei sezioni organizzate in una matrice a sinistra; contenuti nel pannello destro. |
| Mosaic | Sezioni su due righe, poi contenuti su due colonne con geometria e paging condivisi. |

Per Deck 90, su/giù navigano le sezioni, destra/X entra nei contenuti e sinistra torna alle sezioni. Relay usa la matrice in due dimensioni; X o destra al bordo della matrice entra nell'elenco. Mosaic usa la matrice e permette di scendere nei contenuti dalla seconda riga. Aperture usa la fascia inferiore e permette di salire nei contenuti. Tutti i temi mantengono il touch sulle forme effettivamente disegnate.

Le forme sono state riviste: ruota con tessellazione più fine e punti memorizzati, connessioni che non attraversano il centro, focus a riferimenti angolari e distinzione fra sezione attiva e area che riceve input. Le dimensioni di righe, celle, contorni e bersagli touch derivano dagli stessi helper e rispettano il mini-player. Nessun nuovo shader, sfondo animato o font aggiuntivo.

## Compatibilità e verifiche

Il formato delle impostazioni resta V3, 40 byte. Il terzo bit del tema usa una posizione prima libera e conserva i quattro ID precedenti; le tre nuove preferenze griglia/elenco usano altri bit liberi. I test verificano riapertura, errori di scrittura, valori non validi e conservazione delle altre impostazioni. Legacy mantiene la palette precedente.

Le verifiche sul Mac comprendono:

- ASan/UBSan su preferenze, geometria, focus, salvataggio e ritorni della navigazione.
- Tutti i 720 ordini delle sezioni, anche con selezione diretta della ruota e input mantenuti.
- Sei temi non Legacy × radio/preferiti, podcast, scoperta, paesi, generi ed episodi, in italiano/inglese e con/senza mini-player. I test percorrono la schermata di produzione, non solo il disegno di un esempio.
- Ritorno dal catalogo, ripristino del genitore degli episodi, podcast offline, passaggio fra sezioni con stick inclinato e ingresso/ritorno dal player.
- Regressioni audio, impostazioni, download, miniature, seek e ritorno dal mini-player.
- Build nativa ottimizzata, release audit e verifica del pacchetto VPK.

Le immagini generate sono rendering host del codice nativo con dati e servizi sostituiti. Non dimostrano fluidità, consumo RAM, qualità del pannello o riproduzione di una radio reale sulla Vita.

## Prove da fare sulla console

1. Ghost Mesh: tenere inclinato lo stick destro attraversando Video → Radio → Podcast → Immagini; muovere contemporaneamente il sinistro e verificare che l'elenco resti indipendente.
2. Aprire e chiudere L1/R1 con stick inclinato, poi riportarlo neutro e riprovare. Verificare anche il blocco del mini-player.
3. In ciascun tema: radio, scoperta, paese/genere, podcast, episodi, riproduzione, ritorno e podcast offline. Nessun ritorno ai tab di Legacy.
4. Verificare focus, touch e nomi lunghi con mini-player e con Riduci animazioni; controllare entrambe le colonne di Mosaic.
5. Cambiare tutti i temi e riavviare l'app; confrontare memoria e fluidità, anche durante riproduzione/download.

Pacchetto e prove: `build/themes-20261023/`. La console non risultava montata durante la preparazione: il VPK resta disponibile localmente per la copia e l'installazione.

VPK SHA-256: `37695f52815c67475d44205b1ad5cfcfc11fdae7cf98f497098accca6d8a2445`.

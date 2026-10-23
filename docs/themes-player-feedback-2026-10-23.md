# Temi dopo le prove sulla console

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

Decisione utente: conservare Legacy, Ghost Mesh, Deck 90 e Signal; rifare i tre
restanti senza cambiare gli ID persistiti. Nessuna nuova sezione navigabile.

## Piano prima del codice

| Tema | Composizione / movimento | Materiali e colori |
|---|---|---|
| Aperture | Un elemento grande alla volta, navigazione verticale; apertura attraverso portali lungo l'asse Z. Il browser in griglia diventa una scheda panoramica. Il menu L1 e un registro, non una ruota | Notte #0e101b, pannello #181e30, titanio #6c98bf, luce #9eb4ff, testo #e8edf4 |
| Relay | Categorie su un tracciato a sinistra, contenuti come stazioni collegate su un percorso a destra; browser a nodi sfalsati. Propagazione di segnale e rete solida tridimensionale | Petrolio #101922, pannello #162832, cavo #70bfd0, segnale #e6ce65, testo #e2efef |
| Mosaic | Un'unica fascia di cinque copertine/schede: sinistra/destra per sfogliare, su/giu per pagina. Le anteprime hanno prospettiva e le aperture si dispiegano come un soffietto | Archivio #171321, cartella #241c31, inchiostro #978bc8, riflesso #dbb6ed, carta #f1e7f5 |

Inter Medium/SemiBold per i titoli, IBM Plex Mono per la strumentazione.
Le geometrie condivise dai temi conservati restano identiche. Riduzione del
movimento conclude subito le aperture e blocca i movimenti ambientali.
Niente blur, nuovi framebuffer, texture scaricate, thread o heap per fotogramma.
Miniature/cache e controlli del player rimangono condivisi.

Controllo del brief: tre colori diversi non bastano; qui cambiano numero di
elementi visibili, flusso di lettura, geometria, struttura del browser e modelli
3D. Le liste compatte restano disponibili quando l'utente sceglie la lista.
Rendering host e test geometrici non costituiscono una valutazione estetica o
di frame time sulla PS Vita; servono nuove prove sulla console.

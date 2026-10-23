# Transizioni continue e materiali dei sette temi

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

23 ottobre 2026. Modifiche successive alla versione standard 1.4.0, conservata
nel suo bundle di release. Il VPK di verifica usa ID `motion-1023`, bolla
`VMD motion-1023`, titolo applicazione `VMDK00001` e log
`ux0:data/VitaMediaDeck/session_motion-1023.txt`.

## Cause e correzioni

Il cambio di identità della pagina riavviava l'entrata di ogni elemento, con
ritardi diversi per slot. Le griglie ripartivano con trasformazioni di posizione
e profondità anche quando l'utente voleva soltanto cambiare menu. L1 sommava
questa entrata a quella del pannello. Il focus dei tab veniva inoltre conservato
nel singolo ciclo della schermata, perdendo continuità fra Home locale e online.

Adesso i contenuti si muovono come una superficie continua. La molla a smorzamento
critico conserva posizione e velocità quando cambia la destinazione: non crea
una coda di animazioni. Le righe uscenti restano disegnate finché attraversano il
viewport; le nuove entrano sullo stesso piano. Mosaic usa una fascia orizzontale
di copertine. Gli ingressi ripetuti dei singoli elementi sono disattivati per
Home, browser e menu L1; i loro oggetti 3D e i fondali continuano a vivere.

Il cambio di cartella/menu introduce un impulso limitato nella camera, senza
azzerare la posizione o nascondere gli elementi. La profondità degli oggetti
risponde allo stesso movimento. Anche una sequenza di identità tutte diverse
e della stessa lunghezza mantiene il movimento entro il limite di 12 pixel.

Rendering, focus e hitbox condividono le trasformazioni. Le righe che entrano
ed escono rimangono interagibili nella regione effettiva della lista; le porzioni
sotto header, footer o mini-player sono ritagliate. I contatori restano fermi.
Il focus dei tab è persistente fra i cicli delle schermate e il focus dei browser
non viene ricreato a ogni apertura di una cartella cachata.

## Linguaggio dei movimenti

Si conservano le palette, i caratteri Inter/IBM Plex Mono e le strutture esistenti.
I colori guida restano il nero, il petrolio, l'ambra, il fosforo, l'avorio e gli
accenti ciano/lilla dei rispettivi temi. Il cambiamento riguarda movimento,
luce e composizione temporale; Legacy resta privo di oggetti 3D.

| Tema | Pagine e focus | Menu ed effetti |
| --- | --- | --- |
| Legacy | Scorrimento verticale sobrio; alone ambra a due fasce | L1 da sinistra; luce periferica lenta e sottile |
| Ghost Mesh | Movimento orbitale leggero e bordi ciano/fosforo | L1 da destra; cursore della ruota continuo e riflesso laterale |
| Deck 90 | Risposta breve e scorrimento da terminale; bordo al fosforo | L1 rapido da sinistra; rimosse le inversioni luminose delle righe |
| Signal | Movimento compatto, inclinazione controllata dei pannelli | Apertura da destra; tagli diagonali e angoli luminosi |
| Aperture | Pannello grande, movimento da pellicola e profondità di camera | Apertura più distesa da sinistra; riflessi simmetrici del diaframma |
| Relay | Nodi sullo stesso piano e piccoli impulsi lungo i percorsi | L1 da sinistra; accenti luminosi sui raccordi |
| Mosaic | Copertine che scorrono orizzontalmente, con cerniere 3D | L1 da destra; luce sul dorso e sul bordo delle copertine |

I parametri di risposta dello scroll variano da 160 a 250 ms; sono parametri
fisici della molla, non una promessa di durata identica per ogni distanza.
I menu adottano tempi e direzioni propri del tema. Non ci sono flash a schermo
intero o testo che deve essere riscritto per diventare utilizzabile.

## Copertura e costo

Collegati: Home locale, file/cartelle locali, browser dei server (inclusi i
cataloghi Navidrome e Immich che usano quel percorso), radio/podcast, selettore
di cartella destinazione ed elenco dei server. Le azioni L1/R1 e i player usano
il focus aggiornato. Gli inserimenti/rimozioni delle pagine della cache online
ricalibrano la posizione della vista invece di far ripartire un ingresso.

Il selettore di destinazione conserva l'elenco finché non cambia cartella o si
crea una nuova cartella: prima lo rileggeva a ogni frame. Non introduce thread,
connessioni o allocazioni di texture per le transizioni. Lo stato del movimento
è fisso per canale e l'overscan è limitato alla finestra visibile più una riga.
Un salto molto lungo viene limitato a una pagina di corsa, così i comandi non
accumulano secondi di ritardo. La preferenza Riduci movimento elimina gli
spostamenti e mantiene un focus statico leggibile.

## Verifiche

- 490.280 controlli sulla fisica: inversioni rapide, assestamento, contesti
  consecutivi, limiti, refresh, dati vuoti, pausa/ripresa e movimento ridotto.
- 225.416 controlli del renderer nativo sul Mac, con 210 frame browser e 60
  frame Home: sette temi, trasformazioni uniformi, assenza di sovrapposizioni
  delle hitbox, touch durante apertura L1 e aggiunta di dati in cache.
  Nella specifica fixture browser: massimo 10 elementi, 1.440 vertici e 134
  primitive per frame. Questi valori non sono un benchmark dell'app intera.
- 567 controlli sul selettore di cartella effettivo: sei frame senza riletture,
  invalidazione su figlio/padre/creazione, esclusione dei file e annullamento.
- Suite di scene, Home/tab e persistenza, chrome/L1, player audio/pagine,
  navigazione radio/podcast/offline/cache: superate.
- Build nativa Release/LTO e audit VPK: evidenza nel bundle di consegna.

Le anteprime impiegano le funzioni di disegno della UI con font e dati sintetici.
Non sono screenshot della console né misure di FPS, consumi o frame time Vita.

## Verifiche sulla Vita

1. Tenere premute le direzioni sui tab e sulle liste, poi invertire rapidamente:
   contenuti e focus devono continuare a muoversi senza ricomparire tutti insieme.
2. Scorrere cartelle locali e remote, tornare al padre, provare Mosaic/Aperture
   e toccare un elemento durante il movimento: deve aprirsi quello visibile.
   Nel selettore destinazione, creare una cartella e controllare l’elenco aggiornato.
3. Passare fra Radio, Podcast, offline e altre sezioni con miniature in cache;
   aprire/chiudere L1 e R1 durante la transizione e con il mini-player presente.
4. Attivare Riduci movimento: nessuno scroll inerziale o movimento ambientale;
   controllare contrasto del glow e fluidità sulla GPU effettiva della console.

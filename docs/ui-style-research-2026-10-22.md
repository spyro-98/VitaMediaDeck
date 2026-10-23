# VitaMediaDeck — ricerca visiva e correzione griglie, 22 ottobre 2026

> Dated implementation/research record. Integrated status: [VitaMediaDeck 1.4.1](releases/1.4.1.md); [validation guide](VALIDATION.md). Build IDs, figures and pending checks below describe the recorded snapshot. External research links were not revalidated for this patch release.

Aggiornamento: i quattro temi sono ora integrati. Vedi [temi nativi e verifiche](themes-2026-10-22.md). Il seguito documenta la precedente fase di ricerca e confronto.


## Stato della scelta

L'utente chiede una trasformazione della composizione, oltre al colore: più cyberdeck, Ghost in the Shell e grafica tecnologica anni Novanta, con basso costo di rendering. Le prime proposte Ghostline, Phosphor e Vector restano studi utilizzabili, ma non costituiscono una direzione approvata. Le nuove proposte Ghost Mesh, Deck 90 e Signal cambiano navigazione, gerarchia e presentazione dei file. Nessuno dei nuovi temi è attivato nel VPK.

## Campione Vita / PSP

La ricerca confronta progetti pubblici, raccolte e discussioni recenti. Non è una classifica di popolarità: i post Reddit mostrano preferenze individuali; gli archivi PSP contengono anche temi storici. Un tema LiveArea modifica il launcher della console e non determina automaticamente la UI interna di un'applicazione.

| Riferimento | Cosa suggerisce per questa app |
| --- | --- |
| [RetroFlow Launcher](https://github.com/jimbob4000/RetroFlow-Launcher) | Copertine protagoniste, scorrimento e cache, personalizzazione coerente. È un riferimento concreto per navigazione con controller. |
| [XMBFlow Launcher](https://github.com/mersolt/XMBFlow-Launcher) | Reinterpretazione della navigazione XMB; utile studiare una categoria dominante e il contesto delle categorie vicine. Non è prova della diffusione del progetto. |
| [VitaOS](https://github.com/hotdog-face/VitaOS) | Un altro tentativo di ridisegnare l'intera esperienza Vita. Interessante come confronto di struttura e presentazione dei media. |
| [Discussione PSP su wallpaper e temi](https://www.reddit.com/r/PSP/comments/1rzo0ge/style_check_what_is_your_wallpapertheme/) | Le risposte mostrano interesse sia per XMB originale sia per personalizzazioni CXMB. La familiarità e la rapidità contano insieme all'aspetto. |
| [Tema PSP ispirato alla PS3 precedente al firmware 3.00](https://www.reddit.com/r/PSP/comments/1wmx7c3/ps3_theme_pre_300_for_psp/) | Onde, font e icone possono costruire un'identità riconoscibile senza una dashboard fitta di pannelli. |
| [PSPunk: PTF](https://www.pspunk.com/psp-themes/) e [CTF, tra cui CyberTech e Wipeout Remix](https://www.pspunk.com/psp-ctf-themes/13/) | Archivio di composizioni differenti, non soltanto wallpaper: assi orizzontali, lettering e geometrie aggressive. Sono riferimenti storici, non nuove uscite del 2026. |
| [Preferenze temi Vita](https://www.reddit.com/r/PSVita/comments/1pk8bwk/whats_the_best_theme_for_the_ps_vita/) e [temi per Vita nera](https://www.reddit.com/r/VitaPiracy/comments/1ttx5qn/what_theme_will_fit_my_black_ps_vita/) | Compaiono riferimenti a Edgerunners, Evangelion/NERV e anime. Prendere il linguaggio grafico; sviluppare asset originali. |
| [Presentazione Imago](https://www.reddit.com/r/VitaPiracy/comments/1xby1kc/i_built_imago_a_modern_ps_vita_theme_manager_with/) | Esempio recente di theme manager con ricerca e gestione visuale. Le affermazioni su immagini ridotte e caricamento in background sono dell'autore, non misure eseguite da noi. |

## Cyberdeck, Ghost in the Shell, grafica anni Novanta

| Fonte | Principio da esplorare |
| --- | --- |
| [Falcema, Ghost and the Shell / Nerve Net, 2026](https://falcema.com/works/ghost-and-the-shell/) | Linguaggio circolare, contenuto principale dominante e informazioni che si ramificano intorno. I pannelli cambiano secondo il contenuto. È il riferimento principale per Ghost Mesh. |
| [Cyberdeck con interfaccia ispirata a GITS](https://www.reddit.com/r/cyberDeck/comments/1t6xefe/ui_design_for_cyberdeck/) | Fosfori verdi e carattere da dispositivo dedicato. L'autore descrive un'app Pygame: non è un benchmark né una libreria Vita. |
| [Tema GITS per Hyprland](https://github.com/electrocrem/gits) | Palette scura, gerarchia tecnica, accenti e tipografia. Ridurre la quantità di finestre per lo schermo 960×544. |
| [James Brocklebank, GITS](https://www.jamesbrocklebank.com/ghost-in-the-shell) e [Territory Studio, GITS](https://territorystudio.com/project/ghost-in-the-shell/) | Segnali, connessioni, scala e organizzazione delle informazioni. Si tratta del lavoro per il film del 2017: non attribuirlo all'anime del 1995. |
| [WipEout, testimonianze dei designer](https://www.creativereview.co.uk/an-oral-history-of-wipeout/) | Identità costruita con lettering, icone nitide, diagonali e ritmo visivo. È il riferimento di Signal; non una copia di un menu esistente. |
| [eDEX-UI](https://github.com/GitSquared/edex-ui) e [cool-retro-term](https://github.com/Swordfish90/cool-retro-term) | Terminali e palette fosfori/ambra come vocabolario. Non importare Electron, Qt o shader CRT nell'app Vita; eDEX-UI è archiviato. |
| [Arwes](https://arwes.dev/docs) | Frames e transizioni possono suggerire un linguaggio, senza adottare il framework web. |
| [IBM Plex](https://github.com/IBM/plex) e [Oldschool PC Fonts](https://int10h.org/oldschool-pc-fonts/readme/) | Possibili famiglie tipografiche per studi futuri. Verificare leggibilità sul pannello Vita e includere le licenze se si adottano font. Nessun font aggiunto al pacchetto in questo passaggio. |

## Tre nuove composizioni

- **Ghost Mesh:** Home come mappa circolare delle sezioni; contesto e azione sulla sinistra. Il browser conserva anteprime distinte e un focus essenziale. I collegamenti rappresentano categorie reali, senza statistiche decorative. È la proposta da approfondire per il carattere GITS.
- **Deck 90:** selettore di sezioni laterale, directory e file al centro, selezione inversa, carattere monospaziato e palette da display hardware. Privilegia nomi e percorsi rispetto alle miniature.
- **Signal:** tipografia grande, contrasti chiari/scuri, colore arancio e geometria diagonale. I file diventano righe grafiche numerate con gerarchia molto esplicita. Il fondo chiaro richiede anche una valutazione del consumo sui modelli OLED.

Le anteprime consentono selezione delle sezioni, apertura di una cartella dimostrativa, selezione dei file e ritorno alla Home. Non riproducono media. Verificate in Chrome alle larghezze 1024, 736, 390 e 320 px; nessun errore JavaScript, comandi e limiti geometrici verificati. Il layout verticale stretto serve alla consultazione in chat; la composizione da sviluppare sulla console resta orizzontale 960×544. Le dimensioni mostrate nei file sono contenuto dimostrativo, non una scansione aggiornata della memoria Vita.

## Realizzazione e prestazioni

La prossima implementazione dipende dalla direzione scelta. Palette, font, icone e regole di focus devono formare un unico sistema, mantenendo intatti decoder, audio, sottotitoli, annullamento e mini-player. Evitare una combinazione indiscriminata delle tre composizioni.

Obiettivi progettuali, ancora da misurare: geometria 2D e texture già in cache; nessuna allocazione per frame; fino a sei miniature visibili; eventuale atlas 512×512 RGBA pari a 1 MiB; transizioni finite intorno a 120 ms con Reduce motion. Nessun blur multipass, distorsione CRT o animazione continua di particelle come impostazione predefinita. Misurare frame time e memoria sulla Vita anche durante riproduzione e download. La verifica browser non dimostra FPS nativi.

## Correzioni native consegnate indipendentemente dal tema

- Due righe di altezza uguale calcolate sul viewport realmente disponibile, anche con mini-player. Home: 231 px per scheda, oppure 199 px con mini-player. Browser cartelle: 209 / 177 px.
- Il contorno della selezione può occupare i tre pixel esterni alla scheda anche sul bordo superiore; lo spazio è riservato sotto l'header e sopra il limite inferiore. Eliminato il doppio allargamento della selezione nella Home.
- Icone segnaposto centrate nella nuova area anteprima. Rendering, paging, hit test e richiesta miniature condividono la stessa geometria; i browser mostrano due righe complete.
- Test focus/geometria su 1.455 combinazioni di viewport; 972 controlli della Home nativa renderizzata sull'host; regressioni audio (1.703), impostazioni/guide (2.720) e download (319) passate. Build nativa ottimizzata e release audit passati. Nessuna misura di fluidità hardware in questo passaggio.

Pacchetto: `build/layout-review-20261022/VitaMediaDeck.vpk`.
SHA-256: `c53bea2436ba8712f63b91cb11368113211abf4634ee84f2add697aab2fef282`.

Sulla Vita restano da verificare prima e seconda riga, bordo superiore dentro una cartella, navigazione touch/D-pad con mini-player aperto e ritorno dal player. Il nuovo stile resta una scelta aperta, non un tema già integrato.

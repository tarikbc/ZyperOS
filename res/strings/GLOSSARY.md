# ZyperOS's words: the rules and the glossary

Every string in `res/strings/*.txt` follows this page, in every language, so
one thing has one name everywhere (DESIGN.md 12). I approved the terms in
the languages lab (2026-11-07, `docs/design/mock/languages-lab.html`) for the
first five; German, Japanese and Italian came the same day, their terms
written to match. Change a term here first, then everywhere it is used.

## The eight

| Code | Name in its own words | Says | Notes |
|---|---|---|---|
| `en` | English | you | The source. Title Case for headings, rows and named choices. |
| `zh-Hans` | 简体中文 | 你 | Mainland Simplified Chinese. Drawn in Noto Sans SC (tools/cjkfont.py). |
| `es` | Español | tú | Latin American, neutral: *computadora*, *celular*, *video*. |
| `pt-BR` | Português (Brasil) | você | Brazilian: *tela*, *arquivo*, *celular*, *baixar*. I review it. |
| `fr` | Français | vous | France, as Nintendo's French. |
| `de` | Deutsch | du | As Nintendo's German. Nouns capitalized, as German always has them. |
| `ja` | 日本語 | です・ます | Drawn in Noto Sans JP (tools/cjkfont.py). Friendly and polite, never stiff. |
| `it` | Italiano | tu | As Nintendo's Italian. |

## Rules for every language

- **Never translated:** ZyperOS, Zyper, Diatom, Muse, MP4 (the video
  player's name), PortMaster, RetroAchievements, ScreenScraper, SteamGridDB,
  Wi-Fi, Bluetooth, USB, SSH, PIN, Linux, macOS, Windows, system names
  (Game Boy, SNES, PlayStation...), core names, shader names
  (Sharp-Shimmerless), game names, file and folder names on the card (Roms,
  BIOS), and URLs.
- **Button names as printed on the handheld, never translated:** A, B, X, Y,
  L1, R1, L2, R2, START, SELECT, MENU, VOL+, VOL-, POWER. The d-pad's
  directions are not printed, so they are translated, in capitals where
  English has them in capitals: UP, DOWN, LEFT, RIGHT.
- **Capital letters:** each language's own (My pick). English: Title
  Case for headings, rows and named choices. Spanish, Portuguese, French
  and Italian: sentence case for those (*Saída de áudio*, *Tiempo de juego*,
  *Écran d'accueil*, *Uscita audio*); German too, its nouns capitalized
  (*Spiele suchen*). Status values in the value column are lowercase in
  every Latin language, as in English (*off*, *desativado*, *non connecté*,
  *aus*), German's nouns again excepted.
- **Wi-Fi** is WLAN in German, as German devices write it; Wi-Fi in every
  other language.
  Messages are sentences and end with a full stop (。 in Chinese).
- **Punctuation:**
  - French puts a no-break space (` `) before `!`, `?`, `:` and `;`.
  - Chinese uses full-width punctuation (，。！？：；（）), and a
    half-width space between Chinese and Latin letters or digits
    (`按 A 确定`, `2 分钟`). Japanese the same (、。！？（）, `A で決定`,
    `2 分`). Dates in both as they are written there, with no spaces
    (`8月23日`).
  - Spanish opens `¡` and `¿`.
  - Every language keeps English's `/` for pairs (`L1/R1`), `+` for chords
    (`MENU+VOL`) and ` · ` between facts.
- **Exclamation marks** only where English has them, which is Zyper's bubble.
- **printf conversions** (`%s`, `%d`, `%.1f`) stay, in English's order, or
  numbered (`%2$s ... %1$d`) where the language needs another order.
  tools/i18n.py fails on any other.
- **Length, and never a cut:** the panel is 1024 x 768 in the launcher's
  units and a row label and its value share about 800. Write the short
  natural form. A text cut with an ellipsis loses its meaning, so no string
  is ever cut *(2026-11-07)*: where a place is narrow, that place has
  a string of its own with a short form that still says the thing ("USB File
  Transfer" on the empty home's card and in MENU is *Transferir USB*, where
  the File Transfer page's heading is *Transferir arquivos por USB*). A UI
  string that slides in a marquee is cut too, only slowly. A block's
  `note = tight` says the room in units. `make check-i18n-shots` fails on
  any cut or slide in any language, and the pseudo-locale shows what a
  longer word would meet (the room each kind of string has is below).
- **Room: each kind of string has a box, and stays inside it** *(2026-11-07)*.
  A string that touches the edge of its box, or runs past it, is broken, not
  only one cut at the screen's edge: the Computer Connected question's hint
  ran past its panel in Portuguese. What room a string has:
  - *A heading* has its panel less the page count beside it (a title may
    take the count's room too, when it has to). A title wider than a dialog
    breaks onto a second line.
  - *A row's label and its value* share the panel's width; neither slides.
  - *A tile or a key* has its own width, in units (`note = tight: ...`). A
    tile's two lines fall to the smaller size before they touch its edge.
  - *A hint or a legend* (`Left/Right: move   A: choose`) has the panel's
    inner width, and breaks between its keys, at the three spaces, onto a
    second line. Keep the three spaces: they are where it may break.
  - *A message* is one line in the message panel; a long one is a sentence
    of its own with a short form, never a marquee.
  - *The corner, the footer and the legend line* have the screen between its
    insets, and their parts must not meet.

  `make check-i18n-shots` measures it: every screen of tools/i18n-shots.py
  is drawn in all eight languages and `xx`, and src/ui.c checks each string
  it draws against the smallest rect it sits on (the panel, tile or plate),
  the neighbouring text on its line, and the screen. It fails with the shot,
  the language, the string ID and the overflow in pixels (units of 1024). A
  layout fix (wrap, balance, size to the content) comes before a shorter
  string; where a string must be shorter, keep it natural in that language.
  A string no shot draws fails the check too, unless tools/i18n-unshot.txt
  lists it (that list may only shrink): add a shot of the screen it is on.
  `xx` (every string 40% longer) only warns, as a longer word to expect.
- **Counts** use the plural forms (`en.one`, `en.other`); Chinese and
  Japanese have one form. French's one form is for 0 and 1.
- **Unsure?** Add `note = review: ...` to the block saying what to check.

## Terms

| English | 简体中文 | Español | Português (Brasil) | Français | Deutsch | 日本語 | Italiano |
|---|---|---|---|---|---|---|---|
| game / games | 游戏 | juego / juegos | jogo / jogos | jeu / jeux | Spiel / Spiele | ゲーム | gioco / giochi |
| card (the SD card) | 存储卡 | tarjeta | cartão | carte | Karte | カード | scheda |
| system (a console) | 系统 | sistema | sistema | système | System | システム | sistema |
| home (screen) | 主屏幕 | inicio | início | accueil | Start | ホーム | Home |
| shelf (a system's games) | 游戏列表 | lista de juegos | lista de jogos | liste des jeux | Spieleliste | ゲーム一覧 | elenco giochi |
| MENU (the ZyperOS menu) | MENU | MENU | MENU | MENU | MENU | MENU | MENU |
| Language | 语言 | Idioma | Idioma | Langue | Sprache | 言語 | Lingua |
| Appearance | 外观 | Apariencia | Aparência | Apparence | Erscheinungsbild | 外観 | Aspetto |
| Light / Dark / Auto | 浅色 / 深色 / 自动 | Claro / Oscuro / Automático | Claro / Escuro / Automático | Clair / Sombre / Auto | Hell / Dunkel / Automatisch | ライト / ダーク / 自動 | Chiaro / Scuro / Automatico |
| MENU's tabs: Games / Display / Sound / Connections / System / About | 游戏 / 显示 / 声音 / 连接 / 系统 / 关于 | Juegos / Pantalla / Sonido / Conexiones / Sistema / Acerca de | Jogos / Tela / Som / Conexões / Sistema / Sobre | Jeux / Affichage / Son / Connexions / Système / À propos | Spiele / Anzeige / Ton / Verbindungen / System / Info | ゲーム / 画面 / サウンド / 接続 / システム / 情報 | Giochi / Schermo / Audio / Connessioni / Sistema / Info |
| Theme (the look: accent, backgrounds, corners, Zyper) | 主题 | Tema | Tema | Thème | Design | テーマ | Tema |
| Accent (the focus's color) | 强调色 | Color de acento | Cor de destaque | Couleur d'accent | Akzentfarbe | アクセント | Colore d'accento |
| Auto Hours | 自动时段 | Horario auto | Horário auto | Horaires auto | Auto-Zeiten | 自動の時間帯 | Orari automatici |
| on / off (a value) | 开启 / 关闭 | activado / desactivado | ativado / desativado | activé / désactivé | an / aus | オン / オフ | attivo / disattivo |
| not connected | 未连接 | sin conexión | não conectado | non connecté | nicht verbunden | 未接続 | non connesso |
| connecting | 正在连接 | conectando | conectando | connexion… | verbindet … | 接続中 | connessione… |
| needs Wi-Fi (a value) | 需要 Wi-Fi | requiere Wi-Fi | requer Wi-Fi | Wi-Fi requis | braucht WLAN | Wi-Fi が必要 | richiede Wi-Fi |
| Connect to Wi-Fi first. | 请先连接 Wi-Fi。 | Conéctate al Wi-Fi primero. | Conecte-se ao Wi-Fi primeiro. | Connectez-vous d'abord au Wi-Fi. | Verbinde dich zuerst mit dem WLAN. | 先に Wi-Fi に接続してください。 | Connettiti prima al Wi-Fi. |
| Continue (in-game menu) | 继续 | Continuar | Continuar | Reprendre | Weiter | 続ける | Continua |
| Save / Load (a state) | 存档 / 读档 | Guardar / Cargar | Salvar / Carregar | Sauvegarder / Charger | Speichern / Laden | セーブ / ロード | Salva / Carica |
| save state(s) | 即时存档 | estado guardado | save state | sauvegarde d'état | Speicherstand | ステートセーブ | stato salvato |
| saves (in-game save files) | 游戏存档 | partidas guardadas | saves | sauvegardes | Spielstände | セーブデータ | salvataggi |
| Reset / Quit | 重置 / 退出 | Reiniciar / Salir | Reiniciar / Sair | Réinitialiser / Quitter | Neustart / Beenden | リセット / 終了 | Riavvia / Esci |
| Play / Resume | 开始 / 继续 | Jugar / Continuar | Jogar / Continuar | Jouer / Reprendre | Spielen / Fortsetzen | プレイ / 再開 | Gioca / Riprendi |
| Display (in-game) | 画面 | Pantalla | Tela | Affichage | Anzeige | 画面 | Schermo |
| Shader (in-game, a system's menu, MENU) | 着色器 | Shader | Shader | Shader | Shader | シェーダー | Shader |
| Controls | 按键说明 | Controles | Controles | Commandes | Steuerung | 操作方法 | Comandi |
| Achievements (the row and its page) | 成就 | Logros | Conquistas | Succès | Erfolge | 実績 | Obiettivi |
| Favorites | 收藏 | Favoritos | Favoritos | Favoris | Favoriten | お気に入り | Preferiti |
| Recently Played | 最近玩过 | Jugados recientemente | Jogados recentemente | Joués récemment | Zuletzt gespielt | 最近プレイしたゲーム | Giocati di recente |
| Box Art | 封面 | Carátulas | Capas | Jaquettes | Cover | パッケージ画像 | Copertine |
| Video Snaps | 视频预览 | Avances en video | Prévias em vídeo | Aperçus vidéo | Videovorschau | 動画プレビュー | Anteprime video |
| Scraper | 资料抓取 | Scraper | Capas e vídeos | Scraper | Cover und Videos | 画像と動画 | Copertine e video |
| Wi-Fi File Transfer (the page; MENU and the empty home use the short form) | Wi-Fi 文件传输 | Transferir archivos por Wi-Fi; short: Transferir Wi-Fi | Transferir arquivos por Wi-Fi; short: Transferir Wi-Fi | Transfert de fichiers Wi-Fi; short: Transfert Wi-Fi | WLAN-Dateiübertragung; short: WLAN-Transfer | Wi-Fi ファイル転送 | Trasferimento file via Wi-Fi; short: Trasferisci Wi-Fi |
| USB File Transfer (the same) | USB 文件传输 | Transferir archivos por USB; short: Transferir USB | Transferir arquivos por USB; short: Transferir USB | Transfert de fichiers USB; short: Transfert USB | USB-Dateiübertragung; short: USB-Transfer | USB ファイル転送 | Trasferimento file via USB; short: Trasferisci USB |
| file / files | 文件 | archivo / archivos | arquivo / arquivos | fichier / fichiers | Datei / Dateien | ファイル | file / file |
| folder | 文件夹 | carpeta | pasta | dossier | Ordner | フォルダ | cartella |
| phone / computer | 手机 / 电脑 | celular / computadora | celular / computador | téléphone / ordinateur | Handy / Computer | スマホ / パソコン | telefono / computer |
| download / upload | 下载 / 上传 | descargar / subir | baixar / enviar | télécharger / envoyer | herunterladen / hochladen | ダウンロード / アップロード | scaricare / caricare |
| Play Time | 游戏时间 | Tiempo de juego | Tempo de jogo | Temps de jeu | Spielzeit | プレイ時間 | Tempo di gioco |
| Search Games | 搜索游戏 | Buscar juegos | Buscar jogos | Rechercher un jeu | Spiele suchen | ゲームを検索 | Cerca giochi |
| Random Game | 随机游戏 | Juego al azar | Jogo aleatório | Jeu au hasard | Zufallsspiel | ランダムゲーム | Gioco a caso |
| Date & Time | 日期和时间 | Fecha y hora | Data e hora | Date et heure | Datum und Uhrzeit | 日付と時刻 | Data e ora |
| Clock (12-hour / 24-hour) | 时钟（12 小时制 / 24 小时制） | Reloj (12 horas / 24 horas) | Relógio (12 horas / 24 horas) | Horloge (12 heures / 24 heures) | Uhr (12 Stunden / 24 Stunden) | 時計（12 時間表示 / 24 時間表示） | Orologio (12 ore / 24 ore) |
| Auto Off | 自动关机 | Apagado automático | Desligamento automático | Arrêt auto | Automatisch aus | 自動電源オフ | Spegnimento automatico |
| Charge Limit | 充电上限 | Límite de carga | Limite de carga | Limite de charge | Ladelimit | 充電上限 | Limite di carica |
| Side Lights | 侧灯 | Luces laterales | Luzes laterais | Lumières latérales | Seitenlichter | サイドライト | Luci laterali |
| Home Screen | 主屏幕样式 | Pantalla de inicio | Tela inicial | Écran d'accueil | Startbildschirm | ホーム画面 | Schermata Home |
| Spotlight / Gallery (layouts) | 聚焦 / 画廊 | Destacado / Galería | Destaque / Galeria | À la une / Galerie | Spotlight / Galerie | スポットライト / ギャラリー | In evidenza / Galleria |
| System Order | 系统顺序 | Orden de sistemas | Ordem dos sistemas | Ordre des systèmes | Systemreihenfolge | システムの並び順 | Ordine dei sistemi |
| A to Z / Release Date | A 到 Z / 发行日期 | A a Z / Fecha de lanzamiento | A a Z / Data de lançamento | A à Z / Date de sortie | A bis Z / Erscheinungsdatum | A〜Z / 発売日 | Dalla A alla Z / Data di uscita |
| Battery Icon | 电池图标 | Icono de batería | Ícone da bateria | Icône de batterie | Akkusymbol | バッテリーアイコン | Icona della batteria |
| Audio Output | 音频输出 | Salida de audio | Saída de áudio | Sortie audio | Tonausgabe | 音声出力 | Uscita audio |
| speaker / wired / Bluetooth / USB | 扬声器 / 有线 / 蓝牙 / USB | altavoz / con cable / Bluetooth / USB | alto-falante / com fio / Bluetooth / USB | haut-parleur / filaire / Bluetooth / USB | Lautsprecher / Kabel / Bluetooth / USB | スピーカー / 有線 / Bluetooth / USB | altoparlante / cuffie con filo / Bluetooth / USB |
| Audio Channels (stereo / mono) | 声道（立体声 / 单声道） | Canales de audio (estéreo / mono) | Canais de áudio (estéreo / mono) | Canaux audio (stéréo / mono) | Audiokanäle (Stereo / Mono) | 音声チャンネル（ステレオ / モノラル） | Canali audio (stereo / mono) |
| UI Sounds (Wood / Glass / Bubble) | 界面音效（木 / 玻璃 / 泡泡） | Sonidos (Madera / Cristal / Burbuja) | Sons da interface (Madeira / Vidro / Bolha) | Sons de l'interface (Bois / Verre / Bulle) | Bedientöne (Holz / Glas / Blase) | 操作音（ウッド / グラス / バブル） | Suoni dell'interfaccia (Legno / Vetro / Bolla) |
| Menu Music | 菜单音乐 | Música del menú | Música do menu | Musique du menu | Menümusik | メニュー音楽 | Musica del menu |
| volume / brightness | 音量 / 亮度 | volumen / brillo | volume / brilho | volume / luminosité | Lautstärke / Helligkeit | 音量 / 明るさ | volume / luminosità |
| Show Tutorial / tutorial | 查看教程 / 教程 | Ver tutorial / tutorial | Ver tutorial / tutorial | Voir le tutoriel / tutoriel | Tutorial ansehen / Tutorial | チュートリアルを見る / チュートリアル | Mostra tutorial / tutorial |
| Developer Mode | 开发者模式 | Modo desarrollador | Modo desenvolvedor | Mode développeur | Entwicklermodus | 開発者モード | Modalità sviluppatore |
| System Update | 系统更新 | Actualización del sistema | Atualização do sistema | Mise à jour du système | Systemupdate | システムアップデート | Aggiornamento di sistema |
| About ZyperOS | 关于 ZyperOS | Acerca de ZyperOS | Sobre o ZyperOS | À propos de ZyperOS | Über ZyperOS | ZyperOS について | Informazioni su ZyperOS |
| battery | 电池 | batería | bateria | batterie | Akku | バッテリー | batteria |
| Battery too low | 电量过低 | Batería muy baja | Bateria muito fraca | Batterie trop faible | Akku zu schwach | バッテリー残量不足 | Batteria troppo scarica |
| Plug in a charger. | 请连接充电器。 | Conecta un cargador. | Conecte um carregador. | Branchez un chargeur. | Schließe ein Ladegerät an. | 充電器を接続してください。 | Collega un caricabatterie. |
| Charging / charged | 正在充电 / 已充满 | Cargando / cargada | Carregando / carregada | En charge / chargée | Lädt / geladen | 充電中 / 充電完了 | In carica / carica |
| Getting hot | 设备发热 | Se está calentando | Esquentando | Ça chauffe | Wird heiß | 本体が熱くなっています | Si sta scaldando |
| Hold POWER to start | 按住 POWER 开机 | Mantén POWER para encender | Segure POWER para ligar | Maintenez POWER pour démarrer | Halte POWER zum Starten | POWER 長押しで起動 | Tieni premuto POWER per accendere |
| Turn off / Restart | 关机 / 重启 | Apagar / Reiniciar | Desligar / Reiniciar | Éteindre / Redémarrer | Ausschalten / Neu starten | 電源オフ / 再起動 | Spegni / Riavvia |
| Back (B) | 返回 | Volver | Voltar | Retour | Zurück | 戻る | Indietro |
| Cancel / OK / Done | 取消 / 确定 / 完成 | Cancelar / Aceptar / Listo | Cancelar / OK / Pronto | Annuler / OK / Terminé | Abbrechen / OK / Fertig | キャンセル / OK / 完了 | Annulla / OK / Fatto |
| skip | 跳过 | omitir | pular | passer | überspringen | スキップ | salta |
| Rescan Folder | 重新扫描文件夹 | Volver a escanear carpeta | Reescanear pasta | Réanalyser le dossier | Ordner neu einlesen | フォルダを再スキャン | Ripeti scansione cartella |
| Sort By | 排序方式 | Ordenar por | Ordenar por | Trier par | Sortieren nach | 並べ替え | Ordina per |
| Sign in / sign out | 登录 / 退出登录 | Iniciar sesión / cerrar sesión | Entrar / sair | Se connecter / se déconnecter | Anmelden / abmelden | サインイン / サインアウト | Accedi / esci |
| Woof! (Zyper) | 汪！ | ¡Guau! | Au au! | Ouaf ! | Wuff! | ワン！ | Bau! |

## How a string is added

1. Put a block in the file of its screen, `res/strings/<area>.txt`, with
   all eight languages: the ID is the area, then what it is
   (`menu.language`, `tour.lang.hello`).
2. Run `python3 tools/i18n.py` (`make strings`): it writes
   `src/i18n_ids.h`, `src/i18n_tables.c` and `res/web/i18n/`.
3. Use it: `T(S_MENU_LANGUAGE)` in C, `t("name")` on the page for
   `web.name`. `make check-strings` holds every block complete and used.
4. A Chinese or Japanese string with a character its font cut lacks fails
   the check; `make cjk-font CJK_SRC=DIR` cuts them again.
5. A translator who would rather work in a table: `python3 tools/i18n.py
   --export CODE OUT.tsv`, fill the last column, `--import CODE OUT.tsv`.

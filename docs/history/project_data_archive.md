---
name: project-data-archive
description: "data::archive — module d'archives modulaire (2026-09-13, étendu 2026-09-19) : lecture ET écriture EN FLUX de zip, gzip, xz, bzip2, zstd, tar (4 compressions), 7z et ISO 9660, lecture de RAR 1.5 à 7 (volumes, chiffrement) ; codecs deflate/LZMA/LZMA2/bzip2/zstd/PPMd H et I ; VirtualFs + Navigator, liens symboliques créés à l'extraction ; validé contre 7-Zip, UnRAR, bsdtar, GNU tar, zstd, bzip2, Python, liblzma et fuzzé sous ASan."
metadata:
  type: project
---

Troisième demande de la session emulator_demo ([[project-emulator-demo]]).
Remplace le fichier unique `data/archive.hpp` écrit pour lancer une ROM depuis
une archive (DEFLATE/gzip/zip/tar en lecture seule).

prompt :
```
Améliore la section archive du module data afin de le rendre modulaire avec une interface qui permet de
désarchiver ou dézziper n'importe quel format d'archive et d'empaqueter ou zipper n'importe quel format
d'archive en ajouter un système de navigation de répertoire/fichier dans l'archive et ajouter la
possibilité d'ouvrir une archive cryptée avec un mot de passe. Créer plusieurs fichiers pour chaque type
d'archive et pour les méthode utiles. Utilise le wrapper c++ sdl3 iostream pour lire et écrire les
fichiers binaire en respactant strictement le format little-endian ou big-endian (ReadU8, ReadU16Le,
ReadU32Le, ReadU64Le, WriteU8, WriteU16Le, WriteU32Le, WriteU64Le, ...)
archive.hpp / archive/archive_crc.hpp : crc32, crc16 / archive/archive_crypto.hpp : cryptage AES-256 /
archive/archive_fs.hpp (file system) / archive/archive_zip.hpp (et gzip) compression LZMA et LZMA2
cryptage AES-256 / archive/archive_tar.hpp (et tar.gz) / archive/archive_7z.hpp / archive/archive_iso.hpp
```

## Fichiers (lib/include/data/)

| Fichier | Contenu |
|---|---|
| `archive.hpp` | façade : `DetectFormat` (octets, y compris RAR/zip auto-extractibles), `FormatFromExtension`, `OpenArchive/File/Bytes` (tar compressé reconnu en décompressant 512 octets ; volumes RAR via `DiskVolumeOpener`), `CreateWriter`, `WriteArchive/Bytes/File` (fichier `.partial` puis renommage), `PackPath`, `ExtractAll` en flux (`ExtractOptions` : liens, écrasement), classe `Archive` (lecteur + `VirtualFs` + `Navigator`, `Read`, `OpenFile` en flux, `ConvertTo`) |
| `archive/archive_io.hpp` | `BinaryReader`/`BinaryWriter` (erreur collante) sur `sdl3::IOStream` : U8/U16/U32/U64 Le/Be, `U16LeBe`/`U32LeBe` (ISO, moitiés comparées) ; `MemoryStream`, `ArchiveSource` (fichier ou mémoire), `ViewStream`, `ReadRange` borné ; UTF-16 LE/BE, CP437 ; dates DOS/FILETIME/civiles |
| `archive/archive_crc.hpp` | CRC-32 (slicing-by-8), CRC-16 ARC/MODBUS/CCITT, CRC-64 xz |
| `archive/archive_crypto.hpp` | AES-128/192/256 (FIPS-197), CBC, CTR WinZip ; SHA-1, SHA-256, HMAC, PBKDF2-HMAC-SHA1 ; `SecureRandomBytes` ; ZipCrypto |
| `archive/archive_deflate.hpp` | inflate (+ Deflate64), deflate (LZ77 paresseux, Huffman dynamique limité à 15 bits) |
| `archive/archive_lzma.hpp` | LZMA, LZMA2, `.lzma`, `.xz` (multi-flux, CRC32/CRC64/SHA-256), compression et décompression |
| `archive/archive_filters.hpp` | BCJ x86/ARM/ARMT/ARM64, Delta, décodeur BCJ2 |
| `archive/archive_types.hpp` | `Format`, `Compression`, `Encryption`, `EntryInfo`, `ReadOptions` (`memoryCacheLimit`), `WriteOptions` (`ppmdOrder`, `ppmdMemoryMb`, `solidBlockSize`), `ErrorKind` + `ArchiveError` |
| `archive/archive_stream.hpp` | `ArchiveStream` (flux SDL + état d'erreur typé partagé), `DecoderImpl` (Produce/Restart : reculer = recommencer), `EncoderImpl` (Consume/Finish), `InputBuffer`, `SlidingWindow`, fenêtres (`OpenSubStream`, partagées), mémoire partagée, `CheckedStreamImpl` (taille + CRC), concaténation, comptage, `CopyStreamExactly`, `DrainImpl` ; interfaces `ArchiveReader` (`OpenEntry` en flux, `Extract`) / `ArchiveWriter` |
| `archive/archive_bzip2.hpp` | bzip2 en flux (décodage et encodage) |
| `archive/archive_zstd.hpp` | Zstandard RFC 8878 en flux (FSE, Huffman, XXH64 ; dictionnaires non gérés) |
| `archive/archive_ppmd.hpp` | PPMd H (Ppmd7 : 7z, RAR 3) et I rév. 1 (Ppmd8 : zip), identiques octet pour octet à 7-Zip |
| `archive/archive_gzip.hpp` | gzip en flux (membres multiples) |
| `archive/archive_compressed.hpp` | détection/ouverture des compressions, `SingleFileReader/Writer` (.gz .xz .bz2 .zst) |
| `archive/archive_platform.hpp` | liens symboliques sur disque via `std::filesystem` (surcharges `error_code`) |
| `archive/archive_rar.hpp` | `RarReader` (lecture seule), `RarVolumeName` ; décompresseurs adaptés d'UnRAR (paragraphe de licence obligatoire recopié en tête) |
| `archive/archive_fs.hpp` | `NormalizePath` (refus « zip slip » → `UNSAFE_PATH`), `MatchGlob` (`*`, `?`, `[a-z]`, `**`), `VirtualFs` (dossiers implicites, fichiers paresseux, `AddDiskPath`, `Tree`), `Navigator` (cd/ls/resolve/glob) |
| `archive/archive_zip.hpp` | `ZipReader`/`ZipWriter` |
| `archive/archive_tar.hpp` | `TarReader`/`TarWriter` (TAR, TAR_GZIP, TAR_XZ, TAR_BZIP2, TAR_ZSTD) |
| `archive/archive_7z.hpp` | `SevenZipReader`/`SevenZipWriter` |
| `archive/archive_iso.hpp` | `IsoReader`/`IsoWriter` |

`archive/archive_rar.hpp` : fichier vide créé par l'utilisateur avant la
première phase, rempli à la seconde (lecteur RAR).

## Seconde phase (2026-09-19) : flux, nouveaux codecs, RAR

Demande : « Continue d'implémenter les modifications restantes : bzip2, PPMd,
zstd et RAR. […] Mémoire : tout est traité en mémoire (tar.gz et blocs solides
7z entiers). Liens symboliques : ExtractAll les ignore, car SDL ne sait pas en
créer. Dérivation de clé 7z : elle est lente (2^19 SHA-256), environ 1 s sous
ASan. »

- **Flux partout** : chaque codec/déchiffreur est un `sdl3::IOStreamImpl`
  (`IOStream::FromImpl`) ; une entrée = pile de flux (fenêtre → AES →
  décompresseur → filtre → contrôle). Écrivains en flux (zip : en mémoire
  jusqu'à 1 Mio pour le repli « stockée », sinon correction de l'en-tête local
  ou descripteur de données ; tar/ISO : taille annoncée puis copie exacte ;
  7z : blocs solides de `solidBlockSize`, sortie non navigable composée en
  mémoire). Flux compressé à accès aléatoire (tar compressé, bloc solide 7z,
  suite solide RAR) : en mémoire s'il tient dans `memoryCacheLimit`
  (64 Mio), sinon relu (reculer = recommencer).
- **Liens** : `ExtractAll` crée les liens EN DERNIER (aucun fichier écrit à
  travers un lien), refuse les cibles qui sortent du dossier, remplace un lien
  préexistant au lieu de le suivre ; `AddDiskPath` archive les liens comme liens.
- **7zAES** : SHA-NI + lots de 1 024 tours + `KeyCache` → 63 ms sous ASan.
- **RAR** : RAR 4 et 5 (+ RAR 7 : dictionnaires non puissance de deux arrondis,
  distances étendues), SFX, volumes (.partN.rar / .rNN), solide, chiffrement
  1.3/1.5/2.0/3.x/5 et en-têtes chiffrés, CRC-32 / BLAKE2sp (HMAC si
  « HashMAC »), liens Unix/Windows/jonctions/physiques/copies. Les boucles
  d'UnRAR reprennent au début après chaque écriture (reprise ajoutée à
  unpack15). Dictionnaire limité à 1 Gio (`UNSUPPORTED` au-delà).

## Ce que fait chaque format

- **zip** — lecture : EOCD, ZIP64 (localisateur cherché même si les champs 32
  bits ne sont pas saturés : Info-ZIP l'écrit pour les entrées lues en flux),
  préfixe auto-extractible (décalage déduit), UTF-8/CP437, modes et liens Unix,
  horodatage 0x5455 ; méthodes 0/8/9/14/95 ; WinZip AES AE-1/AE-2 128-256 bits
  (vérificateur PUIS HMAC avant tout déchiffrement utile), ZipCrypto.
  Écriture : mêmes méthodes (repli sur « stockée » si pas de gain), AES-256
  AE-2 ou ZipCrypto, ZIP64 automatique.
- **tar** — ustar prefix, GNU `L`/`K`/`D`, pax `x`/`g` (path, linkpath, size,
  mtime), base 256, liens physiques résolus, fichiers creux GNU `S` + pax
  GNU.sparse 0.1 et 1.0 (trous en zéros). Écriture ustar + pax si nécessaire,
  enregistrement de 10 240 octets. tar.gz/tar.xz décompressés en mémoire.
- **7z** — graphe de codeurs GÉNÉRIQUE (le flux compressé alimente le codeur 0 ;
  liaison InIndex = entrée d'un codeur, OutIndex = sortie d'un autre — vérifié
  sur de vrais en-têtes 7-Zip : AES = codeur 0, LZMA2 = codeur 1, liaison (1,0) ;
  BCJ2 = 4 entrées, 4 flux compressés) ; Copy/LZMA/LZMA2/Deflate/Deflate64/AES/
  BCJ/BCJ2/ARM/ARMT/ARM64/Delta ; en-têtes encodés et chiffrés ; cache du bloc
  solide décodé ET de l'échec (sinon un mauvais mot de passe relance 2^19
  SHA-256 par fichier). Écriture : solide ou non, Copy/Deflate/LZMA/LZMA2,
  AES-256 (7zAES:19, IV aléatoire, sel vide comme 7-Zip), en-tête LZMA
  éventuellement chiffré.
- **ISO 9660** — lecture Rock Ridge (NM, PX, SL, TF, CE, CL/RE) sinon Joliet
  sinon noms ISO ; multi-extents. Écriture : arbre primaire avec Rock Ridge
  (SP+ER sur « . » racine, zones de continuation quand un enregistrement
  dépasse 255 octets), noms ISO niveau 2 dédoublonnés, Joliet UCS-2 (103
  car., sans liens), tables de chemins L/M des deux arbres.

## Validation (outils indépendants, dans les deux sens)

- zip écrits (4 méthodes × 3 chiffrements) : `7z t` OK et extraction 7-Zip
  identique ; Info-ZIP `unzip -t` OK pour stockée/deflate/ZipCrypto (LZMA/XZ
  non compilés dans unzip) ; Python zipfile OK. Lus : 7-Zip AES-128/192/256
  (+LZMA), LZMA avec/sans EOS, Deflate64, ZipCrypto, Info-ZIP (-e, ZIP64
  forcé, flux stdin, liens -y), préfixe non ajusté ET ajusté (`zip -A`),
  Python deflate/LZMA. Mot de passe manquant/faux et altération (HMAC) détectés.
- tar/tar.gz/tar.xz écrits : bsdtar, GNU tar, Python tarfile (chemins de 370
  caractères, cibles longues). Lus : GNU (gnu/posix/ustar/v7), bsdtar
  (pax/gnutar), Python (GNU/PAX/USTAR, mtime 9·10^10, nom Unicode long), fichiers
  creux GNU/pax 0.1/pax 1.0 (12 Mio, 40 segments) identiques.
- 7z écrits (4 méthodes × solide/non × aucun/AES/AES+en-tête) : 24/24 `7z t`
  et extraction identique (liens, permissions). Lus : défaut, ultra (BCJ2 sur
  un ELF de 10 Mio), non solide, LZMA, Deflate, Deflate64, Copy, BCJ, BCJ2,
  ARM, ARMT, ARM64, Delta:4, AES, AES + en-tête chiffré, liens -snl — contenus
  identiques ; BZip2/PPMd refusés `UNSUPPORTED`.
- ISO écrite : isoinfo -d/-R/-J, osirrox (extraction identique, liens, nom de
  200 car. via CE, 13 niveaux), `7z t`. Lues : genisoimage -R -J / -J / brut
  (8.3) / -R avec relogement, xorriso -R -J / niveau 3.
- Contre liblzma/zlib : voir les octets identiques et ratios dans l'historique
  (deflate 46 821 contre 46 707 pour zlib sur 183 Kio ; LZMA 7-12 % moins bon
  que liblzma, décodage croisé OK).
- Fuzzing ASan/UBSan : 6 000 zip corrompus, 1 500 tar/tar.gz/tar.xz, 3 150
  ISO/7z, 500 7z chiffrés — aucun défaut mémoire. Seconde phase : 30 000
  archives mutées (RAR 1.5-5, 7z PPMd/BZip2/BCJ2, zip PPMd/AES, tar.bz2,
  tar.zst, .zst, tar creux).
- Seconde phase :
  - codecs : bzip2 (82 contrôles, `bzip2 -t`), zstd (227, `zstd -t`),
    Ppmd7 28/28 et Ppmd8 30/30 identiques octet pour octet à 7-Zip, LZMA 323,
    fichier de 24 Mio en flux pour chaque codec ;
  - zip écrit, 7 méthodes × 3 chiffrements + sortie en tube : `7z t` OK
    (zstd absent du 7-Zip standard), bsdtar pour bzip2/zstd/PPMd ; lus :
    7-Zip BZip2/PPMd/Deflate64/LZMA/AES-128/256, Info-ZIP ZipCrypto/flux/liens ;
  - tar : 5 variantes écrites relues par GNU tar et bsdtar, lecture en flux
    sans cache, fichiers creux GNU/pax 0.0/0.1/1.0 de GNU tar ;
  - 7z : 7 méthodes × (solide, non solide, AES, AES + en-tête) relus avec et
    sans cache ; lus : PPMd, BZip2, BCJ2, PPC, SPARC, IA64, ARMT, ARM64,
    Delta, Deflate64, blocs multiples, en-tête chiffré ;
  - RAR : 106 fixtures (libarchive + rarfile) comparées fichier par fichier à
    UnRAR 7 compilé localement, avec cache et sans : 83 identiques, les 23
    autres sont refusées aussi par UnRAR (corrompues ou mot de passe inconnu)
    et AUCUN fichier extrait par UnRAR ne manque (dont la fixture PPMd/LZ de
    240 Mio) ;
  - `tests/archive_smoke_test.cpp` : 29 tests (+ RAR 1.5/2.0/2.9/5 embarqués,
    liens créés/refusés, flux par morceaux sans cache, codecs par morceaux).
- Façade : chaîne zip→7z→tar.xz→iso→tar.gz→tar→zip identique à la source.
- `tests/archive_smoke_test.cpp` : 24 tests (vecteurs FIPS/RFC, fixtures
  Python + 7-Zip + liblzma embarquées, allers-retours de chaque format,
  zip slip), ~8 s sous ASan (dérivations 7zAES).
- emulator_demo : ROM lancée depuis un 7z à en-tête chiffré, un zip AES
  7-Zip, une ISO, un tar.xz, un .gbc.xz.

## Bugs payés pendant le travail (à ne pas refaire)

1. SDL3 refuse `SDL_IOFromFile("/dev/urandom")` (« not a regular file or
   pipe ») → aléa via getrandom / arc4random_buf / BCryptGenRandom.
2. ZipCrypto : `uint16_t temp; temp * (temp ^ 1)` promu en `int` → débordement
   signé (UBSan) ; calcul en `uint32_t`.
3. zip : ZIP64 ignoré quand l'EOCD 32 bits n'est pas saturé → faux décalage
   « auto-extractible » sur les zip d'Info-ZIP écrits en flux.
4. 7z : `kEnd` de SubStreamsInfo et `kEnd` de StreamsInfo sont DEUX octets ; en
   n'en lisant qu'un, la liste des fichiers était prise pour la fin de
   l'en-tête → archives « valides » à ZÉRO entrée (les tests « passaient » sur
   une liste vide — toujours compter les entrées). Symétrique à l'écriture :
   `kEnd` final manquant dans l'en-tête encodé → 7-Zip « Headers Error ».
5. Détection du format puis lecteur qui lit « depuis la position courante » →
   signature introuvable ; chaque lecteur se repositionne à 0.
6. Réservations `reserve(tailleAnnoncée)` dans inflate/LZMA : un en-tête
   mensonger aurait alloué des Gio ; plafonnées par la taille d'entrée.
7. Écrivain ISO : tailles de répertoires mesurées sans Rock Ridge et tables de
   chemins construites avant le placement des secteurs (corrigés avant le
   premier essai, par relecture).
8. La ligne de commande d'emulator_demo est recopiée dans le rapport :
   `--archive-password=***` masqué.

## Ajouts hors du module

- `sdl3::IOStream` : `FromDynamicMemory`, lectures/écritures BE et signées,
  `ReadExact`/`WriteExact`, `Flush`, `DynamicMemoryBytes`, **`Close()`** (rend
  le statut de SDL_CloseIO : le destructeur ne peut pas signaler un disque plein).
- emulator_demo : `--archive-password=MOT` (`SetArchivePassword`), formats
  7z/iso/xz/tar.xz dans la boîte ROMs, « bloc compressé partagé » quand la
  taille compressée d'une entrée n'existe pas (solide, tar.gz).

## Bugs payés pendant la seconde phase

9. zstd : table de distribution par défaut des longueurs de correspondance
   fausse (2 essais) — retrouvée par simulation Python d'un flux ; descriptions
   de séquences à entrelacer dans l'ordre LL, OF, ML.
10. PPMd : écart « inexplicable » sur un ELF = 7-Zip avait ajouté BCJ ;
    Ppmd8 zip : 7-Zip écrit un marqueur de fin (-1) avant de vider le codeur.
11. RAR 3 PPMd : le codeur de Subbotin découpe le contexte binaire en 2^14
    parts ; pour le symbole 1, `range = (range>>14)*(2^14-size0)` et non
    `range - bound` (l'ancien `Ppmd7RarRangeDecoder` avait la même erreur).
12. zip AES : le code HMAC n'était vérifié qu'en lisant le flux déchiffré
    jusqu'au bout ; un décodeur deflate qui s'arrête à la taille annoncée ne
    le faisait jamais (AE-2 sans CRC) → altération non détectée. Corrigé par
    `DrainImpl` (le reste du flux authentifié est lu à la fin de l'entrée).
13. 7z : tailles des sous-flux avec PLUSIEURS dossiers solides (l'ancien code
    n'en avait qu'un) — le dernier fichier de chaque dossier était pris pour
    le premier du suivant ; en-tête chiffré → faussement « mot de passe
    incorrect ».
14. zip : LZMA + descripteur de données refusé par 7-Zip sans marqueur de fin
    → marqueur toujours écrit (bit 1), comme 7-Zip.
15. Heredoc non protégé (`<<EOF`) : les `backticks` des commentaires
    exécutés par le shell — toujours `<<'EOF'` pour du code.
16. libarchive (bsdtar) échoue sur un zstd dans un zip quand la taille est un
    multiple de 128 Kio (fin de trame jamais vue) : défaut de libarchive, pas
    du module.

## Limites connues

- Non gérés : zip multi-volumes, UDF, El Torito, dictionnaires zstd, RAR 1.4
  (« RE~^ »), écriture RAR (propriétaire), filtres RAR 3 non standard (UnRAR
  non plus) ; écriture ISO > 4 Gio (multi-extents lus seulement) ; pas de
  filtres BCJ à l'écriture 7z ; pas de liens physiques à l'écriture tar.
- Taille inconnue d'avance (.bz2, .xz multi-flux) : tar/ISO lisent ce
  contenu en mémoire pour écrire son en-tête.
- Liens sous Windows : mode développeur ou droits d'administrateur requis
  (échec compté dans `skippedLinks`).
- 7z : pas de vérificateur de mot de passe dans le format ; un mauvais mot de
  passe est déduit d'un flux indécodable ou d'un CRC faux (`WRONG_PASSWORD`).
- Cryptographie en temps non constant ; ZipCrypto est cassé (lecture de vieux
  zip uniquement, écriture possible mais déconseillée).

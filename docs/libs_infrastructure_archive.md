# archive — archives et compression

`libs/infrastructure/archive` · couche **infrastructure** · `libarchive.a` · espace de noms `archive::`

## Présentation

`archive` lit et écrit les formats d'archive et de compression courants
**sans aucune bibliothèque externe**. Tous les codecs sont implémentés dans le
module :

| Format | Lecture | Écriture |
|---|---|---|
| zip (ZIP64, AES WinZip AE-2, ZipCrypto ; store, deflate, deflate64, LZMA, bzip2, zstd, PPMd) | ✔ | ✔ |
| 7z (solide, LZMA, LZMA2, PPMd, bzip2, deflate, filtres BCJ, BCJ2, Delta, AES-256, en-têtes chiffrés) | ✔ | ✔ |
| tar (ustar, pax, GNU, fichiers creux) et tar.gz, tar.xz, tar.bz2, tar.zst | ✔ | ✔ |
| ISO 9660 + Joliet + Rock Ridge | ✔ | ✔ |
| Fichiers compressés seuls : `.gz` (membres multiples), `.xz`, `.lzma`, `.bz2`, `.zst` | ✔ | ✔ |
| RAR 1.5 à 7 (volumes, archives solides, chiffrement) | ✔ | — (format propriétaire) |

**Fonctionnement en flux** : une entrée de plusieurs gigaoctets s'extrait
avec quelques mégaoctets de mémoire.

**Système de fichiers virtuel** : l'archive se présente comme une
arborescence (`VirtualFs`), avec ses dossiers implicites reconstitués. On y
navigue comme dans un shell (`Navigator` : `cd`, liste, motifs glob).

**Sécurité** :

- tout chemin qui sortirait de la racine (`../`, chemin absolu, lecteur
  Windows) est refusé : c'est la protection contre le « zip slip » ;
- les liens symboliques sont créés en dernier et ne peuvent pas pointer hors
  du dossier d'extraction ;
- une limite de taille protège contre les bombes de décompression.

**Erreurs** : aucune exception. Les fonctions rendent
`Result<…, ArchiveError>`.

**Utilisateurs** : l'[émulateur](demo_emulator.md) (ROM dans une archive),
[`net`](libs_infrastructure_net.md) (transfert de fichiers),
[`voxel`](libs_infrastructure_voxel.md) (mods et sauvegardes) et
[`script_bindings`](libs_infrastructure_script_bindings.md).

## Architecture interne

| Fichier | Contenu |
|---|---|
| `archive.hpp` | Façade : détection, lecture et écriture génériques, `ExtractAll`, classe `Archive` |
| `archive_types.hpp` | `Format`, `Compression`, `Encryption`, `EntryType`, `EntryInfo`, `ReadOptions`, `WriteOptions`, `ErrorKind`, `ArchiveError` |
| `archive_io.hpp` | `Bytes`, lecteur et écrivain binaires (petit et grand boutiste) sur `sdl3::IOStream`, UTF-16, CP437, dates, `ArchiveSource` |
| `archive_stream.hpp` | `ArchiveStream`, décodeurs et encodeurs en flux, interfaces `ArchiveReader` / `ArchiveWriter` |
| `archive_fs.hpp` | `NormalizePath`, `MatchGlob` (`*`, `?`, `[a-z]`, `**`), `VirtualFs`, `Navigator` |
| `archive_crc.hpp` | CRC-32, CRC-16, CRC-64 (xz), XXH64 (zstd) |
| `archive_crypto.hpp` | AES (ECB, CBC, CTR), SHA-1, SHA-256, HMAC, PBKDF2, aléa sûr, ZipCrypto |
| `archive_deflate.hpp`, `archive_lzma.hpp`, `archive_bzip2.hpp`, `archive_zstd.hpp`, `archive_ppmd.hpp`, `archive_filters.hpp` | Codecs |
| `archive_gzip.hpp`, `archive_compressed.hpp` | gzip ; fichiers compressés seuls |
| `archive_zip.hpp`, `archive_tar.hpp`, `archive_7z.hpp`, `archive_iso.hpp`, `archive_rar.hpp` | Lecteurs et écrivains par format |
| `archive_platform.hpp` | Liens symboliques sur disque (SDL3 ne sait pas les créer) |

Une entrée lue est une **pile de flux** : fenêtre sur l'archive, puis
déchiffrement, décompression, filtre et contrôle d'intégrité. Chaque étage est
un `sdl3::IOStreamImpl`.

Dépendances : `core`, `sdl3` (pour `IOStream`).

## API publique

### Détection

- `DetectFormat(stream)` reconnaît le format **d'après les octets**, jamais
  d'après l'extension.
- `FormatFromExtension(path)`, `FormatExtension(format)`,
  `HasArchiveExtension(path)`, `IsWritableFormat(format)`,
  `FormatName(format)` et `CompressionName(c)` complètent cette partie.

### Lecture

| Fonction | Rôle |
|---|---|
| `OpenArchiveFile(path, ReadOptions)`, `OpenArchiveBytes(bytes, …)`, `OpenArchive(source, …, nameHint, volumes)` | Ouvre un `ArchiveReader` de n'importe quel format |
| `DiskVolumeOpener(path)` | Volumes suivants d'un RAR multi-volumes |
| `ExtractAll(reader, dossier, ExtractOptions)` | Extraction complète en flux → `ExtractStats` (fichiers, dossiers, liens, octets…). Tous les chemins sont vérifiés **avant** toute écriture |

`ReadOptions` :

- `password` ;
- `verifyChecksums` (vrai par défaut) ;
- `maxEntrySize` : 4 Gio par défaut, contre les bombes de décompression ;
- `memoryCacheLimit` : 64 Mio. En dessous, un flux solide (tar.gz, bloc 7z)
  est gardé en mémoire pour l'accès aléatoire ; au-delà, il est relu depuis le
  début.

### Classe `Archive` (lecteur, arborescence et navigation)

| Membre | Rôle |
|---|---|
| `static Open(path, opts)`, `static FromBytes(bytes, opts, nameHint)` | Ouverture |
| `GetFormat()`, `Entries()` (`EntryInfo`), `SkippedEntries()` | Description |
| `Fs()` (`VirtualFs`), `Nav()` (`Navigator`), `Reader()` | Arborescence et navigation |
| `HasEncryptedEntries()`, `SetPassword(mdp)` | Chiffrement ; l'arborescence est reconstruite |
| `Read(chemin)` → `Bytes`, `OpenFile(chemin)` → `ArchiveStream`, `Extract(index)` | Contenu, avec un chemin absolu ou relatif au répertoire courant |
| `ExtractAll(dossier, opts)` | Extraction |
| `ConvertTo(chemin, format, opts)` | Réécriture dans un autre format (zip → 7z, iso → tar…) |

### Écriture

| Fonction | Rôle |
|---|---|
| `WriteArchiveFile(path, format, fs, WriteOptions)` | Écrit dans un fichier temporaire voisin puis le renomme : une archive existante n'est jamais laissée à moitié écrite |
| `WriteArchiveBytes(format, fs, opts)`, `WriteArchive(format, fs, stream, opts)` | En mémoire, ou dans un flux |
| `PackPath(cheminDisque, cheminArchive, format, opts)` | Archive un fichier ou un dossier ; le format est déduit de l'extension |
| `CreateWriter(format)` | Écrivain bas niveau |

`WriteOptions` :

- `compression` : `STORE`, `DEFLATE`, `LZMA`, `LZMA2`, `BZIP2`,
  `ZSTD` ou `PPMD` ;
- `level` (0 à 9) ;
- `encryption` : `NONE`, `AES256` ou `ZIP_CRYPTO`. Ce dernier est
  faible et ne sert qu'à la compatibilité ;
- `password` ;
- pour 7z : `encryptHeaders`, `solid`, `solidBlockSize` ;
- `dictionarySize`, `ppmdOrder`, `ppmdMemoryMb` ;
- `volumeLabel` (ISO) et `defaultTime`.

### `VirtualFs` et `Navigator`

**`VirtualFs`**

- **Ajout** : `AddFile(chemin, octets)`, `AddLazyFile(chemin, taille, ouvreur)`,
  `AddDirectory`, `AddSymlink`, `AddDiskPath(cheminDisque, cheminArchive)`.
- **Gestion** : `Remove`, `Find`, `Children`, `Walk`, `Flatten`,
  `Glob`, `FileCount`.
- **Contenu** : `OpenContent`, `ReadContent`.
- **Affichage** : `Tree()` produit un arbre en texte.

**`Navigator`**

- `Cwd()`, `ChangeDirectory(chemin)` ;
- `List(chemin)`, `Glob(motif)` ;
- `Resolve(chemin)`, `Absolute(chemin)`.

### Erreurs

`ArchiveError {kind, message}` :

- `Describe()` rend un message lisible.
- `NeedsPassword()` indique s'il faut un mot de passe.
- `kind` (`ErrorKind`) vaut `IO`, `FORMAT`, `CORRUPT`,
  `UNSUPPORTED`, `PASSWORD_REQUIRED`, `WRONG_PASSWORD`, `UNSAFE_PATH`,
  `LIMIT` ou `INVALID_ARGUMENT`.

## Exemple

Créer une archive 7z chiffrée en mémoire, la relire et y naviguer :

```cpp
#include <cstdio>
#include <archive/archive.hpp>

static archive::Bytes ToBytes(const char *text) {
	return archive::Bytes(text, text + std::strlen(text));
}

int main() {
	// 1. Construire une arborescence virtuelle
	archive::VirtualFs fs;
	(void)fs.AddFile("saves/slot1.json", ToBytes("{\"level\": 3}"));
	(void)fs.AddFile("saves/slot2.json", ToBytes("{\"level\": 7}"));
	(void)fs.AddFile("readme.txt", ToBytes("Sauvegardes du jeu"));

	// 2. L'écrire en 7z chiffré
	archive::WriteOptions options;
	options.compression = archive::Compression::LZMA2;
	options.encryption = archive::Encryption::AES256;
	options.password = "secret";
	auto written = archive::WriteArchiveBytes(archive::Format::SEVEN_ZIP, fs, options);
	if (!written) {
		std::fprintf(stderr, "%s\n", written.Error().Describe().CStr());
		return 1;
	}
	std::printf("7z : %zu octets\n", written.Value().size());

	// 3. La relire : format détecté d'après les octets. Les en-têtes ne sont
	//    pas chiffrés (encryptHeaders = false) : l'ouverture réussit, seul le
	//    contenu demande le mot de passe.
	auto opened = archive::Archive::FromBytes(written.Value());
	if (!opened)
		return 1;
	archive::Archive &arc = opened.Value();
	if (arc.HasEncryptedEntries())
		arc.SetPassword("secret");
	std::printf("format %s, %zu entrées\n", archive::FormatName(arc.GetFormat()), arc.Entries().size());

	// 4. Naviguer comme dans un système de fichiers
	archive::Navigator &nav = arc.Nav();
	(void)nav.ChangeDirectory("saves");
	for (archive::VirtualFs::NodeId id : nav.Glob("*.json"))
		std::printf("  %s\n", arc.Fs().PathOf(id).CStr());
	auto content = arc.Read("slot2.json"); // relatif au répertoire courant
	if (!content)
		std::printf("erreur : %s\n", content.Error().Describe().CStr());
	else
		std::printf("slot2 : %.*s\n", int(content.Value().size()), reinterpret_cast<const char *>(content.Value().data()));
	return 0;
}
```

Sortie :

```
7z : 275 octets
format 7z, 4 entrées
  saves/slot1.json
  saves/slot2.json
slot2 : {"level": 7}
```

Avec `encryptHeaders = true`, c'est l'ouverture elle-même qui échoue
(`opened.Error().NeedsPassword()`). Il faut alors passer le mot de passe
dans `ReadOptions`.

Extraire une archive sur disque :

```cpp
auto arc = archive::Archive::Open("roms.zip");
if (arc) {
	auto stats = arc.Value().ExtractAll("data/emulator/roms");
	if (!stats)
		std::printf("%s
", stats.Error().Describe().CStr());
}
```

## Intégration

`modules = ["archive"]`, puis `#include <archive/archive.hpp>` (façade).

- Le portage du code RAR, adapté d'UnRAR, recopie en tête de fichier le
  paragraphe de licence obligatoire. Ce format reste en **lecture seule**.
- La dérivation de clé 7z (2¹⁹ SHA-256) est volontairement lente : environ
  une seconde sous AddressSanitizer.

## Tests

```sh
cpp-maker test -m archive
```

Les tests vérifient les allers-retours de chaque format et de chaque méthode,
le chiffrement, les chemins dangereux et la navigation. D'après les notes de
développement, le module a été validé contre 7-Zip, UnRAR, bsdtar, GNU tar,
zstd, bzip2, Python et liblzma, et passé au fuzzing sous ASan.

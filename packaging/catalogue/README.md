# arospkg author manifests

One file per target, kept by hand, in the format apkg-pack reads. Before a
release, set `version` (or raise `revision` for a new package of the same
version) in the file for each target being released.

## x86_64 ABIv11: part of the release script

`scripts/make-release.sh` builds nothing. After `scripts/build-folio.sh`, it
stages the Folio drawer, copies `folio.x86_64.v11.toml` beside it, and runs
`apkg-pack check`, `apkg-pack build` and `apkg-pack check` on the result.
It stops if the file's version is not `VER`, and checks that every file in
the ZIP is byte-identical to the staged one:

```text
APKG_PACK=/path/to/apkg-pack.pyz VER=0.4.1 scripts/make-release.sh
```

`dist/Folio-<ver>.x86_64-aros-v11.zip` is then ready to upload as it is.
After uploading: `apkg-pack submit <public-https-url> --pr`. Get
apkg-pack.pyz as described in arospkg `docs/guide/authoring.md`.

## Pi and i386: repacked 0.4.1 archives

Folio 0.4.1-aros1 adds the supported embedded manifest to the published
Pi and i386 archives. All original files are byte-identical, including the
executable, icons, licences and BUILD-REPORT.md. No new runtime test is
claimed.

These manifests use the current apkg-pack format, not the proposed RFC
syntax in packaging/manifest.toml.

To reproduce either archive, unpack its original 0.4.1 ZIP in a fresh
directory. Copy the matching TOML file beside the Folio drawer as
Folio.arospkg.toml. With apkg-pack from arospkg commit 595a9ab:

```text
apkg-pack check /path/to/Folio
apkg-pack build /path/to/Folio --epoch 315532800 --output /path/to/new-archive.zip
apkg-pack check /path/to/new-archive.zip
```

The command reads the sibling Folio.info and includes the manifest at
.arospkg/manifest.toml. After uploading the new archive:

```text
apkg-pack submit <public-https-url> --pr
```

Published checksums are in SHA256SUMS-0.4.1-aros1 on release v0.4.1.
Do not replace the original archives.

# arospkg author manifests

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

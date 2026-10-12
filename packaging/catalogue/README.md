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

## Pi and i386: packed by hand from the cross builds

The i386 ABIv0 and Raspberry Pi aarch64 executables come from their own
cross toolchains and MuPDF builds, not from `scripts/build-folio.sh`. Each
package holds the same files as the x86_64 one, with the unstripped
executable and a BUILD-REPORT.md that states the compiler, the source
revision and what was run, instead of SHA256SUMS:

```text
<drawer>/Folio/Folio, Folio.info, README, CHANGELOG, LICENSE, licenses/,
               BUILD-REPORT.md
<drawer>/Folio.info
<drawer>/Folio.arospkg.toml   (copy of folio.<arch>.<abi>.toml)
```

Then, with apkg-pack:

```text
apkg-pack check <drawer>/Folio
apkg-pack build <drawer>/Folio --output Folio-<ver>.<arch>-<abi>.zip
apkg-pack check Folio-<ver>.<arch>-<abi>.zip
```

`check` confirms the executable's CPU and that the icon sets the stack for
the `[launch]` table. After uploading: `apkg-pack submit <public-https-url>
--pr`.

# arospkg author manifests

One file per target, kept by hand, in the format apkg-pack reads. Before a
release, set `version` (or raise `revision` for a new package of the same
version) in the file for each target being released.

| File | Target | Package |
| --- | --- | --- |
| `folio.x86_64.v11.toml` | `one` | `Folio-<ver>.x86_64-aros-v11.zip` |
| `folio.x86_64.v1.toml` | `mainline` | `Folio-<ver>.x86_64-aros-v1.zip` |
| `folio.aarch64.v1.toml` | `raspi` | `Folio-<ver>.aarch64-aros-raspi.zip` |
| `folio.i386.v0.toml` | none | the 0.5 i386 ABIv0 package; i386 is no longer built |

The `requires_system` libraries differ by ABI and come from the
executable's automatically opened libraries (`nm` shows them as
`__aros_libset_*Base`), less the ones every system has in ROM (exec, dos,
graphics, intuition, utility): ABIv11 links `crt`, `m` and `stdlib`
libraries, ABIv1 `posixc`, `stdc` and `stdcio`. Both also need
`muimaster`, `asl`, `iffparse` and `cybergraphics`. `openurl.library` is
opened only for an external link and is optional.

## Making the packages

`scripts/make-release.sh` builds nothing. After `scripts/build-folio.sh`
for each target at the commit being released, it stages each Folio
drawer, copies the target's manifest beside it, and runs `apkg-pack check`,
`apkg-pack build` and `apkg-pack check` on the result. It stops if a
manifest's version is not the one in `src/folio.c`, if an executable was
not built from a clean HEAD, or if a file in a ZIP differs from the staged
one:

```text
APKG_PACK=/path/to/apkg-pack.pyz LHA_WRITER=/path/to/lha scripts/make-release.sh
```

`dist/<ver>/` then holds the packages, the source archive and
`SHA256SUMS-<ver>`, ready to upload as they are. After uploading, for each
package: `apkg-pack submit <public-https-url> --pr`. Get apkg-pack.pyz as
described in arospkg `docs/guide/authoring.md`.

The x86_64 executables are stripped with `--strip-unneeded --remove-section
.comment`; the Raspberry Pi one is packed unstripped.

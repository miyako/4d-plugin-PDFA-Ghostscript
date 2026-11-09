# 4d-plugin-PDFA-Ghostscript

PDF to PDF/A-3 conversion plugin for 4D, using a bundled [Ghostscript](https://www.ghostscript.com/) subprocess.

> ⚠️ **License**: Ghostscript is distributed under the [AGPL-3.0](https://www.gnu.org/licenses/agpl-3.0.html).  
> If you distribute this plugin with the bundled `gs` binary, your distribution must comply with AGPL terms (provide source or offer it alongside the binary). Consider a commercial Ghostscript license from [Artifex](https://artifex.com/licensing) for proprietary use.

## Command

```
$result:=PDF TO PDFA($inputPath; $outputPath)
```

| Parameter    | Type | Description                   |
|------------- |------|-------------------------------|
| $inputPath   | Text | POSIX path to input PDF       |
| $outputPath  | Text | POSIX path to output PDF/A-3  |
| $result      | Object | Status object               |

### Return object

| Key           | Type    | Description                         |
|---------------|---------|-------------------------------------|
| success       | Boolean | `true` if conversion succeeded      |
| error         | Number  | Error code (0 = no error)           |
| description   | Text    | Error description or gs stderr      |
| outputPath    | Text    | POSIX path of the created file      |

## How it works

The plugin invokes the bundled `gs` (Ghostscript) binary as a subprocess with these parameters:

```
gs -dPDFA=3 -dBATCH -dNOPAUSE -dNOOUTERSAVE -dCompatibilityLevel=2.0
   -sDEVICE=pdfwrite
   -sColorConversionStrategy=UseDeviceIndependentColor
   -sProcessColorModel=DeviceRGB
   -dPDFACompatibilityPolicy=1
   -sOutputFile=<output>
   PDFA_def.ps
   <input>
```

The `PDFA_def.ps` PostScript preamble configures the ICC profile and conformance level.

## Build

```bash
cd PDFA
mkdir cmake-build && cd cmake-build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Then bundle Ghostscript into the plugin:

```bash
./bundle-gs.sh cmake-build/PDFA.bundle
```

## Validation

```bash
brew install verapdf
verapdf --flavour 3b output.pdf
```

## Release workflows

`VERSION` is authoritative and must be strict `X.Y.Z`, coordinated with
`PDFA/Info.plist`'s `CFBundleVersion` (not an independent build counter).
The migration repaired the stale plist to the existing version without a bump.
The manifest and 4D project schema versions are not release versions.

1. Run **Bump Version** on the desired branch, choosing `patch`, `minor`, or
   `major` in the `bump` input. It validates metadata, commits both files, and
   pushes only that branch without tags. Concurrent bumps on the same branch
   serialize; an advanced branch causes a non-fast-forward failure, not overwrite.
2. Run **Build** separately on that branch. It has no inputs beyond GitHub's
   branch selector. All checkouts use the selected immutable `github.sha`, not
   a moving branch or tag. No main-only publication restriction is imposed.
3. Build rejects invalid/drifting versions and existing tags or releases before
   building Windows x64 and macOS arm64, bundling Ghostscript, verifying the
   merged plugin, signing with Developer ID, and notarizing ZIP/DMG packages.
   The stapled DMG is validated and packages are retained as workflow artifacts.
   Only after these steps succeed does it recheck availability, push a
   non-force `vX.Y.Z` tag at the exact selected SHA, and create a new published
   release via a create-only REST POST with generated notes. Assets are uploaded
   separately without `--clobber`; `gh release create` is deliberately avoided
   because its failure cleanup can delete a temporary draft.

Publication serializes across all branches. API/authentication/network errors
fail closed, and existing tags/releases are never replaced. If release creation
or asset upload fails after tag push, the tag and any created release/uploaded
assets remain. A published release may temporarily have incomplete assets:
inspect the tag, release, assets, and retained package artifacts and recover
manually. Reruns reject that tag; there is no
automatic deletion, overwrite, or rollback. Tag pushes do not start Build.

The existing **Build and Test** workflow remains automatic for pushes and pull
requests targeting `main`; it is separate from manual release publication.
Release signing uses the existing `APPLE_DEVELOPER_ID_CERTIFICATE`,
`APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD`, `KEYCHAIN_PASSWORD`,
`NOTARYTOOL_APPLE_ID`, `NOTARYTOOL_TEAM_ID`, and `NOTARYTOOL_PASSWORD` secrets.

Local lifecycle checks (no native builds, signing, or publication):

```bash
python3 -m pip install -r .github/tests/requirements.txt
python3 -m unittest discover -s .github/tests -v
actionlint .github/workflows/*.yml
```

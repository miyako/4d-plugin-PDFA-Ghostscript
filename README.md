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

Native status-string ownership regression on macOS (Xcode command-line tools,
initialized pinned SDK submodule; no Ghostscript, plugin packaging or signing):

```bash
bash tests/run-status-ownership.sh
```

The harness compiles the actual plugin source and the complete unchanged SDK,
with an engine-boundary shim, ASan/UBSan, and explicit string/handle/object
accounting. It checks exact ASCII, BMP, supplementary and empty content,
allocated/null empty strings, repeated synchronous status calls, pointer-list
allocation failure, and independent successful object handoff. The shim verifies
the JSON Parse input and models an independently owned result; it is not a JSON
parser or certification of the 4D engine, Ghostscript conversion, or Windows.

The ownership audit at `8996d579f85bc35cd8822c28242125b746a3d77e` inventoried
all tracked first-party native code: `PDFA/PDFA-4dplugin.cpp` contains
`PluginMain`, `doPdfToPdfa`, `getBundlePath`, `uniToUtf8`, `returnStatus` and
`shellEscape`. `PDFA/manifest.json` declares only selector 1, two scalar Text
inputs and an Object result; the dispatcher reaches that command and all its
status branches. CMake compiles that translation unit and the pinned
`4D-Plugin-SDK` API; `PDFA.def` exports the entry points. `Info.plist`,
the manifest copies in CMake/bundling/CI, and the 4D test project contain no
additional native commands. There are no first-party dormant native files,
text arrays, callbacks, pointer/object-property helpers or CPicture wrappers.
Ghostscript and SDK internals are third-party audit exclusions.

The compiled SDK is `4d/4D-Plugin-SDK` commit
`cd97d1876d4258b56d2516e8ad2816f4ee93e0b5`, not an inferred release from its
`rev : v14` header. Its `PA_SetStringVariable` aliases the input descriptor;
`PA_ExecuteCommandByID` synchronously forwards parameter pointers without
clearing them. As in the SDK's own `PA_CreateObject` JSON Parse branch,
`returnStatus` must dispose its owned temporary after that call. The successful
object reference is transferred by `PA_ReturnObject`, not cleared locally.
Input parameter strings are borrowed and only read. UTF-16 conversion capacities
and return lengths are bytes, while string lengths are UTF-16 code units.
The same harness fails the immutable baseline on the leaked JSON input and
passes the one-disposal fix. Failure behavior inside the real engine and
unrelated existing error-reporting/JSON formatting limits remain outside this
bounded ownership fix.

PDF/A validation (separate from the ownership regression):

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

Publication serializes across all branches. Availability checks reject both
draft and published releases using the authenticated, fully paginated release
list (the release-by-tag endpoint omits drafts). Only a successful, well-formed
complete listing with no match permits publication; API/authentication/network
or pagination errors fail closed. Existing tags/releases are never replaced. If release creation
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

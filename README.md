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

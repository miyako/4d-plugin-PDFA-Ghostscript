/*
 * PDFA-4dplugin.cpp — Ghostscript subprocess (bundled gs binary)
 *
 * Converts PDF to PDF/A-3 by calling the bundled Ghostscript executable.
 * The gs binary and its shared libraries are embedded inside the plugin bundle,
 * so no system-wide Ghostscript installation is required.
 *
 * License: Plugin code is MIT. The bundled Ghostscript binary is AGPL-3.0
 * (or requires a commercial license from Artifex for proprietary distribution).
 */

#include "4DPluginAPI.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

static void doPdfToPdfa(PA_PluginParameters params);

/* helper: get the plugin bundle path */
static std::string getBundlePath() {
#ifdef __APPLE__
    CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.4d.PDFA"));
    if (!bundle) return "";
    CFURLRef bundleURL = CFBundleCopyBundleURL(bundle);
    if (!bundleURL) return "";
    char path[4096];
    CFURLGetFileSystemRepresentation(bundleURL, true, (UInt8*)path, sizeof(path));
    CFRelease(bundleURL);
    return std::string(path);
#else
    char modulePath[MAX_PATH];
    HMODULE hm = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&doPdfToPdfa, &hm);
    GetModuleFileNameA(hm, modulePath, MAX_PATH);
    std::string p(modulePath);
    /* .../Contents/Windows64/PDFA.4DX → .../Contents */
    size_t pos = p.rfind("\\Windows64\\");
    if (pos == std::string::npos) pos = p.rfind("/Windows64/");
    if (pos != std::string::npos) {
        return p.substr(0, pos + 1); /* includes trailing separator => Contents/ parent */
    }
    return "";
#endif
}

/* helper: convert PA_Unistring to UTF-8 std::string */
static std::string uniToUtf8(PA_Unistring* ustr) {
    if (!ustr) return "";
    PA_long32 len = PA_GetUnistringLength(ustr);
    if (len == 0) return "";
    const PA_Unichar* data = PA_GetUnistring(ustr);
    std::vector<char> buf(len * 4 + 1);
    PA_long32 converted = PA_ConvertCharsetToCharset(
        (char*)data, len * sizeof(PA_Unichar), eVTC_UTF_16,
        buf.data(), (PA_long32)buf.size(), eVTC_UTF_8);
    return std::string(buf.data(), converted);
}

/* helper: return a JSON status object */
static void returnStatus(PA_PluginParameters params, int code, const std::string& message) {
    /* escape special chars in message */
    std::string escaped;
    for (char c : message) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') continue;
        else escaped += c;
    }

    char json[4096];
    snprintf(json, sizeof(json),
        "{\"success\":%s,\"code\":%d,\"message\":\"%s\"}",
        code == 0 ? "true" : "false", code, escaped.c_str());

    std::vector<PA_Unichar> json16(strlen(json) * 2 + 2);
    PA_ConvertCharsetToCharset(
        json, (PA_long32)strlen(json), eVTC_UTF_8,
        (char*)json16.data(), (PA_long32)(json16.size() * sizeof(PA_Unichar)), eVTC_UTF_16);

    PA_Unistring jsonUstr = PA_CreateUnistring(json16.data());

    /* Bypass PA_JsonParse SDK bug (Pitfall #15) */
    PA_Variable cmdParams[2];
    memset(cmdParams, 0, sizeof(cmdParams));
    PA_SetStringVariable(&cmdParams[0], &jsonUstr);
    PA_SetLongintVariable(&cmdParams[1], eVK_Object);
    PA_Variable result = PA_ExecuteCommandByID(1218, cmdParams, 2);

    PA_ReturnObject(params, PA_GetObjectVariable(result));
}

/* shell-escape a path */
static std::string shellEscape(const std::string& path) {
#ifdef _WIN32
    return "\"" + path + "\"";
#else
    std::string escaped = "'";
    for (char c : path) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }
    escaped += "'";
    return escaped;
#endif
}

static void doPdfToPdfa(PA_PluginParameters params) {
    PA_Unistring* inputUstr = PA_GetStringParameter(params, 1);
    PA_Unistring* outputUstr = PA_GetStringParameter(params, 2);

    std::string inputPath = uniToUtf8(inputUstr);
    std::string outputPath = uniToUtf8(outputUstr);

    if (inputPath.empty() || outputPath.empty()) {
        returnStatus(params, -1, "Input and output paths are required");
        return;
    }

    std::string bundlePath = getBundlePath();
    if (bundlePath.empty()) {
        returnStatus(params, -2, "Could not locate plugin bundle");
        return;
    }

    /* Locate bundled gs binary and resources */
#ifdef __APPLE__
    std::string gsPath = bundlePath + "/Contents/Helpers/gs";
    std::string resPath = bundlePath + "/Contents/Resources";
    std::string libPath = bundlePath + "/Contents/Frameworks";
#else
    std::string gsPath = bundlePath + "Windows64\\gswin64c.exe";
    std::string resPath = bundlePath + "Resources";
    std::string libPath = bundlePath + "Windows64";
#endif

    std::string pdfaDefPath = resPath + "/PDFA_def.ps";

    /* Build command */
    std::string cmd;
#ifdef __APPLE__
    /* Set DYLD_LIBRARY_PATH for bundled dylibs and GS_LIB for gs resources */
    std::string gsLibPath = resPath + "/gs_lib:" + resPath + "/gs_Resource/Init";
    cmd = "DYLD_LIBRARY_PATH=" + shellEscape(libPath)
        + " GS_LIB=" + shellEscape(gsLibPath)
        + " " + shellEscape(gsPath);
#else
    /* On Windows, DLLs are found next to the exe; set GS_LIB for resources */
    std::string gsLibPath = resPath + "\\gs_lib;" + resPath + "\\gs_Resource\\Init";
    cmd = "set \"GS_LIB=" + gsLibPath + "\" && " + shellEscape(gsPath);
#endif

    cmd += " -dNOPAUSE -dBATCH -dNOSAFER"
           " -dPDFA=3"
           " -sDEVICE=pdfwrite"
           " -sColorConversionStrategy=RGB"
           " -sProcessColorModel=DeviceRGB"
           " -dPDFACompatibilityPolicy=1"
           " -sOutputFile=" + shellEscape(outputPath)
         + " " + shellEscape(pdfaDefPath)
         + " " + shellEscape(inputPath);

    /* cd to Resources so PDFA_def.ps can find sRGB.icc by relative path */
    std::string fullCmd;
#ifdef _WIN32
    fullCmd = "cd /d " + shellEscape(resPath) + " && " + cmd;
#else
    fullCmd = "cd " + shellEscape(resPath) + " && " + cmd;
#endif

    int exitCode = system(fullCmd.c_str());

    if (exitCode != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "Ghostscript conversion failed (exit code %d)", exitCode);
        returnStatus(params, exitCode, msg);
        return;
    }

    returnStatus(params, 0, "PDF/A-3 conversion successful");
}

/* PluginMain entry point */
#if defined(_WIN32)
#define PLUGIN_EXPORT
#else
#define PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

extern "C" PLUGIN_EXPORT
void PluginMain(PA_long32 selector, PA_PluginParameters params) {
    switch (PA_GetCurrentProcessNumber()) {
        case kInitPlugin:
        case kDeinitPlugin:
            break;
        default:
            switch (selector) {
                case 1:
                    doPdfToPdfa(params);
                    break;
            }
            break;
    }
}

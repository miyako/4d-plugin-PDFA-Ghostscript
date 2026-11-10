#include "4DPluginAPI.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"
#include <cassert>
#include <codecvt>
#include <iostream>
#include <locale>
#include <map>
#include <set>

#ifndef PDFA_SOURCE
#define PDFA_SOURCE "../PDFA/PDFA-4dplugin.cpp"
#endif
#include PDFA_SOURCE

namespace {
std::set<PA_Unichar*> strings;
std::map<PA_Handle, size_t> handles;
struct StatusObject {
    std::u16string json;
};
std::set<StatusObject*> objects;
std::u16string expectedJson;
bool allocatedEmpty = false;
bool failPointerList = false;
size_t parseCalls = 0;
std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> utf8;

std::u16string readString(const PA_Unistring& string) {
    assert(string.fLength >= 0);
    if (!string.fString) {
        assert(string.fLength == 0);
        return {};
    }
    PA_Unichar* buffer = string.fString;
    assert(strings.count(buffer) == 1);
    assert(string.fString[string.fLength] == 0);
    return std::u16string(string.fString, string.fString + string.fLength);
}

void engine(short selector, EngineBlock* eb) {
    switch (selector) {
        case EX_CREATE_UNISTRING: {
            const auto* input = reinterpret_cast<const PA_Unichar*>(eb->fHandle);
            size_t length = 0;
            if (input) while (input[length]) ++length;
            PA_Unistring value = {};
            value.fLength = static_cast<PA_long32>(length);
            if (length || allocatedEmpty) {
                value.fString = new PA_Unichar[length + 1]();
                if (length) std::copy(input, input + length, value.fString);
                PA_Unichar* buffer = value.fString;
                assert(strings.insert(buffer).second);
            }
            eb->fUniString1 = value;
            break;
        }
        case EX_DISPOSE_UNISTRING: {
            PA_Unichar* buffer = eb->fUniString1.fString;
            if (buffer) {
                assert(strings.erase(buffer) == 1);
                delete[] buffer;
            }
            break;
        }
        case EX_HANDLE_MANAGER: {
            PA_Handle handle = eb->fParam1 == 1 ? nullptr : eb->fHandle;
            switch (eb->fParam1) {
                case 1:
                    if (failPointerList) {
                        eb->fHandle = nullptr;
                        eb->fError = 1;
                        break;
                    }
                    eb->fHandle = new char*;
                    *eb->fHandle = new char[eb->fParam2]();
                    handle = eb->fHandle;
                    assert(handles.emplace(handle, static_cast<size_t>(eb->fParam2)).second);
                    break;
                case 2:
                    assert(handles.erase(handle) == 1);
                    delete[] *eb->fHandle;
                    delete eb->fHandle;
                    break;
                case 5:
                    assert(handles.count(handle) == 1);
                    eb->fParam3 = reinterpret_cast<sLONG_PTR>(*eb->fHandle);
                    break;
                case 6:
                    assert(handles.count(handle) == 1);
                    break;
                default:
                    assert(false && "unexpected handle operation");
            }
            break;
        }
        case EX_CONVERT_CHARSET_TO_CHARSET: {
            std::string bytes;
            if (eb->fParam2 == eVTC_UTF_8 && eb->fParam4 == eVTC_UTF_16) {
                const char* input = static_cast<const char*>(eb->fPtr1);
                auto text = utf8.from_bytes(input, input + eb->fParam1);
                bytes.assign(reinterpret_cast<const char*>(text.data()),
                             text.size() * sizeof(char16_t));
            } else {
                assert(eb->fParam2 == eVTC_UTF_16 && eb->fParam4 == eVTC_UTF_8);
                assert(eb->fParam1 % sizeof(PA_Unichar) == 0);
                const auto* input = static_cast<const PA_Unichar*>(eb->fPtr1);
                std::u16string text(input, input + eb->fParam1 / sizeof(PA_Unichar));
                bytes = utf8.to_bytes(text);
            }
            // Both the capacity and returned length are bytes, excluding a terminator.
            assert(bytes.size() <= static_cast<size_t>(eb->fParam3));
            memcpy(eb->fPtr2, bytes.data(), bytes.size());
            eb->fParam3 = static_cast<sLONG_PTR>(bytes.size());
            break;
        }
        case EX_EXECUTE_COMMAND_BY_ID: {
            assert(eb->fParam1 == 1218 && eb->fParam2 == 2);
            auto** arguments = static_cast<PA_Variable**>(eb->fPtr1);
            assert(arguments[0]->fType == eVK_Unistring);
            assert(arguments[1]->fType == eVK_Longint);
            assert(arguments[1]->uValue.fLongint == eVK_Object);
            auto json = readString(arguments[0]->uValue.fString);
            assert(json == expectedJson);
            auto* object = new StatusObject{json};
            assert(objects.insert(object).second);
            PA_SetObjectVariable(static_cast<PA_Variable*>(eb->fPtr2),
                                 reinterpret_cast<PA_ObjectRef>(object));
            ++parseCalls;
            break;
        }
        case EX_CLEAR_VARIABLE: {
            auto* variable = static_cast<PA_Variable*>(eb->fPtr1);
            assert(variable->fType == eVK_Object);
            auto* object = reinterpret_cast<StatusObject*>(PA_GetObjectVariable(*variable));
            assert(objects.erase(object) == 1);
            delete object;
            memset(variable, 0, sizeof(*variable));
            break;
        }
        default:
            std::cerr << "Unexpected engine selector: " << selector << '\n';
            std::abort();
    }
}

void checkStatus(int code, const std::string& message, const std::u16string& json) {
    expectedJson = json;
    PA_ObjectRef output = nullptr;
    PluginBlock params = {};
    params.fResult = &output;
    returnStatus(&params, code, message);
    assert(handles.empty());
    if (failPointerList) {
        assert(output == nullptr);
    } else {
        auto* object = reinterpret_cast<StatusObject*>(output);
        assert(objects.count(object) == 1);
        // The successful output owns independent content after the input is freed.
        std::vector<std::string> churn(128, std::string(4096, 'x'));
        assert(object->json == json);
        PA_DisposeObject(output);
    }
    assert(objects.empty());
    assert(strings.empty() && "returnStatus leaked its JSON Parse input");
}
}

int main() {
    static_assert(sizeof(PA_Unichar) == sizeof(char16_t), "UTF-16 code units");
    gCall4D = engine;
    for (bool emptyRepresentation : {false, true}) {
        allocatedEmpty = emptyRepresentation;
        PA_Unichar empty[] = {0};
        auto value = PA_CreateUnistring(empty);
        assert(readString(value).empty());
        assert(uniToUtf8(&value).empty());
        PA_DisposeUnistring(&value);
        assert(strings.empty());
        assert(uniToUtf8(nullptr).empty());

        const PA_Unichar fixture[] = {'A', 0x65e5, 0xd83d, 0xde00, 0};
        auto text = PA_CreateUnistring(const_cast<PA_Unichar*>(fixture));
        assert(text.fLength == 4);
        assert(uniToUtf8(&text) == "A\xe6\x97\xa5\xf0\x9f\x98\x80");
        PA_DisposeUnistring(&text);

        for (int repeat = 0; repeat < 100; ++repeat) {
            checkStatus(0, "OK", u"{\"success\":true,\"code\":0,\"message\":\"OK\"}");
            checkStatus(-1, "", u"{\"success\":false,\"code\":-1,\"message\":\"\"}");
            checkStatus(2, "A\xe6\x97\xa5\xf0\x9f\x98\x80",
                        u"{\"success\":false,\"code\":2,\"message\":\"A\u65e5\U0001f600\"}");
            checkStatus(-1, "\"\\\n\r",
                        u"{\"success\":false,\"code\":-1,\"message\":\"\\\"\\\\\\n\"}");
        }
        // SDK initializes returned to Undefined when its pointer-list allocation fails.
        failPointerList = true;
        const size_t callsBefore = parseCalls;
        checkStatus(-1, "no call", u"");
        assert(parseCalls == callsBefore);
        failPointerList = false;
    }
    assert(parseCalls == 800);
    std::cout << "PASS: 800 status handoffs; no live SDK strings, handles or objects\n";
}

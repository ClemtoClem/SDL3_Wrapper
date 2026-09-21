#include "settings.hpp"

#include "defines.hpp"
#include "sdl3/filesystem.hpp"
#include "sdl3/iostream.hpp"

namespace emulator_demo {

String Settings::filename = "config.ini";

int Settings::directBoot   = 1;
int Settings::fpsLimiter   = 1;
int Settings::threaded2D   = 0;
int Settings::threaded3D   = 0;
int Settings::highRes3D    = 0;
// Default off: synchronous console/terminal writes are what makes logging
// slow (see sdl3::LogRouter/FileLogSink) — file export runs on its own
// thread regardless and is unaffected by this toggle.
int Settings::logToConsole = 0;
// 0 = horizontal (touch screen on the right), 1 = vertical (stacked).
int Settings::screenLayout = 0;
// Off by default: needs a real ARM7 BIOS/firmware dump to boot otherwise;
// this is an opt-in alternative, not automatic fallback.
int Settings::arm7Hle = 0;
// Off by default: see LOG_VERBOSE (defines.hpp) — hot-path "unknown
// register" diagnostics can otherwise collapse emulation to ~1 FPS.
int Settings::verboseLog = 0;

String Settings::bios9Path    = "bios-firmware/nintendo-nintendo-ds/bios9.bin";
String Settings::bios7Path    = "bios-firmware/nintendo-nintendo-ds/bios7.bin";
String Settings::firmwarePath = "bios-firmware/nintendo-nintendo-ds/firmware.bin";
String Settings::gbaBiosPath  = "bios-firmware/nintendo-game-boy-advance/gba_bios.bin";
String Settings::sdImagePath  = "bios-firmware/sd.img";
// Empty = file export disabled.
String Settings::logFilePath  = sdl3::filesystem::BasePath() + "logs/output.txt";
String Settings::saveDirectory;
std::vector<std::pair<String, String>> Settings::romAliases;


std::vector<Setting> Settings::settings = {
    // isString must match each field's real type below (Settings::load/save
    // reinterpret-cast `value` as `String*` or `int*` accordingly) —
    // these were inverted (ints flagged as strings and vice versa), which
    // corrupted memory on the very first load()/save() call.
    Setting("directBoot", &directBoot, false),  Setting("fpsLimiter", &fpsLimiter, false),
    Setting("threaded2D", &threaded2D, false),  Setting("threaded3D", &threaded3D, false),
    Setting("highRes3D", &highRes3D, false),    Setting("bios9Path", &bios9Path, true),
    Setting("bios7Path", &bios7Path, true),     Setting("firmwarePath", &firmwarePath, true),
    Setting("gbaBiosPath", &gbaBiosPath, true), Setting("sdImagePath", &sdImagePath, true),
    Setting("logFilePath", &logFilePath, true), Setting("logToConsole", &logToConsole, false),
    Setting("screenLayout", &screenLayout, false), Setting("arm7Hle", &arm7Hle, false),
    Setting("verboseLog", &verboseLog, false)};

void Settings::add(std::vector<Setting> platformSettings) {
    settings.insert(settings.end(), platformSettings.begin(), platformSettings.end());
}

bool Settings::load(String filename) {
    Settings::filename = filename;
    auto res = sdl3::IOStream::FromFile(filename, "r");
    if (!res)
        return false;
    sdl3::IOStream settingsFile = std::move(res).Unwrap();

    std::vector<uint8_t> bytes = settingsFile.ReadAll();
    String content(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    // split('\n') already strips the delimiter, unlike the old fgets()-based
    // loop which kept it (and compensated with a "-2" length trim below) —
    // one trailing empty entry after the file's final '\n' is harmless
    // (line.find("=") == npos, no setting name matches it).
    for (const String &line : content.Split('\n')) {
        size_t split = line.Find("=");
        String name  = line.Substr(0, split);
        for (unsigned int i = 0; i < settings.size(); i++) {
            if (name == settings[i].name) {
                String value = split == String::npos ? String() : line.Substr(split + 1);
                if (settings[i].isString)
                    *(String *)settings[i].value = value;
                else if (!value.IsEmpty() && value[0] >= 0x30 && value[0] <= 0x39)
                    *(int *)settings[i].value = int(value.Trim().TryParseInt().UnwrapOr(0));
                break;
            }
        }
    }
    return true;
}

bool Settings::save() {
    auto res = sdl3::IOStream::FromFile(filename, "w");
    if (!res)
        return false;
    sdl3::IOStream settingsFile = std::move(res).Unwrap();

    String out;
    for (unsigned int i = 0; i < settings.size(); i++) {
        String value =
            settings[i].isString ? *(String *)settings[i].value : std::to_string(*(int *)settings[i].value);
        out += settings[i].name;
        out += '=';
        out += value;
        out += '\n';
    }
    settingsFile.Write(out.c_str(), out.GetSize());
    return true;
}

} // namespace emulator_demo


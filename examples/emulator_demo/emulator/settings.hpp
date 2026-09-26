#ifndef SETTINGS_H
#define SETTINGS_H
#include <utility>
#include <vector>

#include "core/core.hpp"

namespace emulator_demo {


struct Setting {
    Setting(String name, void *value, bool isString) : name(name), value(value), isString(isString) {
    }
    String name;
    void       *value;
    bool        isString;
};

class Settings {
  public:
    /// Emplacement par défaut du fichier de réglages et des états sauvegardés :
    /// les sauvegardes des démos vivent dans saves/, séparées des ressources.
    static constexpr const char *DEFAULT_CONFIG_PATH     = "./saves/emulator_demo/config.ini";
    static constexpr const char *DEFAULT_STATE_DIRECTORY = "./saves/emulator_demo/states";

    static void add(std::vector<Setting> platformSettings);
    static bool load(String filename = DEFAULT_CONFIG_PATH);
    /// Écrit les réglages dans getFilename(), en créant son dossier au besoin.
    static bool save();
    /// Fichier lu par load() et écrit par save().
    static String getFilename() {
        return filename;
    }
    static void setFilename(String value) {
        filename = value;
    }
    static int  getDirectBoot() {
        return directBoot;
    }
    static int getFpsLimiter() {
        return fpsLimiter;
    }
    static int getThreaded2D() {
        return threaded2D;
    }
    static int getThreaded3D() {
        return threaded3D;
    }
    static int getHighRes3D() {
        return highRes3D;
    }
    static String getNdsBios9Path() {
        return ndsBios9Path;
    }
    static String getNdsBios7Path() {
        return ndsBios7Path;
    }
    static String getFirmwarePath() {
        return ndsFirmwarePath;
    }
    static String getGbaBiosPath() {
        return gbaBiosPath;
    }
    static String getSdImagePath() {
        return sdImagePath;
    }
    static String getLogFilePath() {
        return logFilePath;
    }
    static int getLogToConsole() {
        return logToConsole;
    }
    /// NDS top/touch screen arrangement: 0 = horizontal (side by side, touch
    /// screen on the right — default), 1 = vertical (stacked, touch below).
    /// No effect on GBA sessions (single screen).
    static int getScreenLayout() {
        return screenLayout;
    }
    /// Runs the ARM7 fully HLE'd (halted, with its BIOS-firmware IPC protocol
    /// stubbed out — see HleArm7) instead of interpreting real ARM7 code, so
    /// NDS sessions can boot without a firmware/ARM7 BIOS dump. NDS-mode only.
    static int getArm7Hle() {
        return arm7Hle;
    }
    /// Gates LOG_VERBOSE() (see defines.hpp): "unknown register/opcode"
    /// diagnostics that can fire on every single interpreted instruction in
    /// a busy-wait loop (e.g. a game polling an unimplemented I/O register).
    /// Off by default — with it on, SDL_Log()'s formatting/locking/syscall
    /// cost in that hot path alone can collapse emulation to ~1 FPS. Enable
    /// only while actively debugging a specific game's missing hardware.
    static int getVerboseLog() {
        return verboseLog;
    }
    static void setDirectBoot(int value) {
        directBoot = value;
    }
    static void setFpsLimiter(int value) {
        fpsLimiter = value;
    }
    static void setThreaded2D(int value) {
        threaded2D = value;
    }
    static void setThreaded3D(int value) {
        threaded3D = value;
    }
    static void setHighRes3D(int value) {
        highRes3D = value;
    }
    static void setNdsBios9Path(String value) {
        ndsBios9Path = value;
    }
    static void setNdsBios7Path(String value) {
        ndsBios7Path = value;
    }
    static void setFirmwarePath(String value) {
        ndsFirmwarePath = value;
    }
    static void setGbaBiosPath(String value) {
        gbaBiosPath = value;
    }
    static void setSdImagePath(String value) {
        sdImagePath = value;
    }
    static void setLogFilePath(String value) {
        logFilePath = value;
    }
    static void setLogToConsole(int value) {
        logToConsole = value;
    }
    static void setScreenLayout(int value) {
        screenLayout = value;
    }
    static void setArm7Hle(int value) {
        arm7Hle = value;
    }
    static void setVerboseLog(int value) {
        verboseLog = value;
    }
    /// Dossier des sauvegardes de cartouche (.sav). Vide = à côté de la ROM
    /// (comportement d'origine). Non enregistré dans le fichier de réglages :
    /// il sert à isoler une exécution de test (emulator_demo --save-dir).
    static String getSaveDirectory() {
        return saveDirectory;
    }
    static void setSaveDirectory(String value) {
        saveDirectory = value;
    }
    /// Dossier des états sauvegardés (sauvegarde rapide F5/F9), enregistré
    /// dans le fichier de réglages. Vide = à côté de la ROM (<rom>.state0).
    static String getStateDirectory() {
        return stateDirectory;
    }
    static void setStateDirectory(String value) {
        stateDirectory = value;
    }
    /// Fichier d'état d'une ROM : <dossier des états>/<nom de la ROM>.state0,
    /// ou <rom>.state0 sans dossier. `logicalPath` : la ROM telle que
    /// l'utilisateur la connaît (à côté de son archive pour une ROM extraite).
    static String stateFilePath(const String &logicalPath) {
        if (stateDirectory.IsEmpty())
            return logicalPath + ".state0";
        size_t slash = logicalPath.Rfind('/');
        String name  = (slash == String::NPOS) ? logicalPath : logicalPath.Substr(slash + 1);
        return (stateDirectory.EndsWith("/") ? stateDirectory : stateDirectory + "/") + name + ".state0";
    }
    /// Déclare que la ROM lue depuis `actualPath` (copie extraite d'une
    /// archive, dans un cache) doit être traitée, pour ses fichiers annexes
    /// (.sav, .cht), comme si elle se trouvait à `logicalPath` (à côté de son
    /// archive). Retiré par unregisterRomAlias à la fin de la session.
    static void registerRomAlias(const String &actualPath, const String &logicalPath) {
        romAliases.emplace_back(actualPath, logicalPath);
    }
    static void unregisterRomAlias(const String &actualPath) {
        std::erase_if(romAliases, [&actualPath](const std::pair<String, String> &alias) { return alias.first == actualPath; });
    }
    /// `romPath` (alias résolu) sans extension : base des fichiers annexes.
    static String romBasePath(const String &romPath) {
        String path = romPath;
        for (const auto &alias : romAliases)
            if (alias.first == romPath)
                path = alias.second;
        size_t dot = path.Rfind('.');
        size_t slash = path.Rfind('/');
        return (dot != String::NPOS && (slash == String::NPOS || dot > slash)) ? path.Substr(0, dot) : path;
    }
    /// romBasePath(), placé dans getSaveDirectory() s'il est défini.
    static String saveBasePath(const String &romPath) {
        String base = romBasePath(romPath);
        size_t slash = base.Rfind('/');
        if (saveDirectory.IsEmpty())
            return base;
        String name = (slash == String::NPOS) ? base : base.Substr(slash + 1);
        return saveDirectory.EndsWith("/") ? saveDirectory + name : saveDirectory + "/" + name;
    }

  private:
    Settings() {}

    static int directBoot;
    static int fpsLimiter;
    static int threaded2D;
    static int threaded3D;
    static int highRes3D;
    static int logToConsole;
    static int screenLayout;
    static int arm7Hle;
    static int verboseLog;

    static String          filename;
    static String          ndsBios9Path;
    static String          ndsBios7Path;
    static String          ndsFirmwarePath;
    static String          gbaBiosPath;
    static String          sdImagePath;
    static String          logFilePath;
    static String          saveDirectory;
    static String          stateDirectory;
    static std::vector<std::pair<String, String>> romAliases;
    static std::vector<Setting> settings;
};

} // namespace emulator_demo

#endif

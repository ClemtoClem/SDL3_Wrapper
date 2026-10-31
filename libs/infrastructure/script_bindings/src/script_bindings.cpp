#include <script_bindings/script_bindings.hpp>

namespace script {

std::vector<String> InstallAll(Interpreter &vm, const BindingsConfig &config) {
    std::vector<String> installed;

    if (config.archive) {
        InstallArchiveLibrary(vm);
        installed.push_back("archive");
    }
    if (config.audioEngine) {
        InstallAudioLibrary(vm, *config.audioEngine, config.audioResolve);
        installed.push_back("audio");
    }
    if (config.ecsWorld) {
        InstallEcsLibrary(vm, config.ecsWorld);
        installed.push_back("ecs");
    }
    if (config.generators) {
        InstallGeneratorLibrary(vm);
        installed.push_back("gen");
    }
    if (config.dataio) {
        InstallDataioLibrary(vm);
        installed.push_back("dataio");
    }
    if (config.physicsWorld) {
        InstallPhysicsLibrary(vm, *config.physicsWorld);
        installed.push_back("physics");
    }
    if (config.canvas3d && config.sceneRoot) {
        InstallRender3DLibrary(vm, config.canvas3d, config.sceneRoot);
        installed.push_back("render3d");
    }
    if (config.sqlDatabase) {
        InstallSqlLibrary(vm, config.sqlDatabase);
        installed.push_back("sql");
    }
    if (config.uiBinding) {
        InstallUiLibrary(vm, config.uiBinding);
        installed.push_back("ui");
    }
    return installed;
}

} // namespace script
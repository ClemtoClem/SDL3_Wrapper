// Définitions de sdl3/process.hpp — fichier généré par splitter.py : le code
// vient tel quel de l'en-tête (seules les signatures sont réécrites).

#include "sdl3/process.hpp"

namespace sdl3 {

// ── Process ──────────────────────────────────────────────────────────────────

Result<Process, Error> Process::Create(const std::vector<String> &args, bool pipeStdio) {
    std::vector<const char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &a : args)
        argv.push_back(a.c_str());
    argv.push_back(nullptr);

    auto *p = SDL_CreateProcess(argv.data(), pipeStdio);
    if (!p)
        return Err(GetError());
    return Ok(Process(p));
}

Result<Process, StringView> Process::CreateWithProperties(SDL_PropertiesID props) {
    auto *p = SDL_CreateProcessWithProperties(props);
    if (!p)
        return Err(GetError());
    return Ok(Process(p));
}

SDL_PropertiesID Process::Properties() const noexcept {
    return m_handle ? SDL_GetProcessProperties(m_handle) : 0;
}

Option<IOStreamView> Process::StdinStream() const noexcept {
    auto *io = m_handle ? SDL_GetProcessInput(m_handle) : nullptr;
    if (!io)
        return NONE;
    return Some(IOStreamView(io));
}

Option<IOStreamView> Process::StdoutStream() const noexcept {
    auto *io = m_handle ? SDL_GetProcessOutput(m_handle) : nullptr;
    if (!io)
        return NONE;
    return Some(IOStreamView(io));
}

Result<ProcessOutput, StringView> Process::Read() {
    if (!m_handle)
        return Err(StringView("process invalide"));
    size_t size = 0;
    int exitCode = 0;
    void *data = SDL_ReadProcess(m_handle, &size, &exitCode);
    if (!data)
        return Err(GetError());

    ProcessOutput out;
    out.data.assign(static_cast<uint8_t *>(data), static_cast<uint8_t *>(data) + size);
    out.exitCode = exitCode;
    SDL_free(data);
    return Ok(std::move(out));
}

Option<int> Process::Wait(bool block) noexcept {
    if (!m_handle)
        return NONE;
    int exitCode = 0;
    if (!SDL_WaitProcess(m_handle, block, &exitCode))
        return NONE;
    return Some(exitCode);
}

} // namespace sdl3

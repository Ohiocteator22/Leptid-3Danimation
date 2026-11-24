// Leptid — optional Ollama AI assistant (pose guidance / app help).
// Off by default: only active when the user asks and an Ollama server is
// reachable. Uses a minimal HTTP client over POSIX TCP sockets (see README
// for the Windows note). Never required for any core feature.
#pragma once
#include <string>
#include <vector>

namespace leptid {

struct OllamaConfig {
    std::string host = "http://localhost";
    int port = 11434;
    std::string model = "llava";
};

// Quick reachability probe (~1 s timeout).
bool ollamaAvailable(const OllamaConfig& cfg);

// Blocking generate call (stream=false). Returns false + error text on failure.
bool ollamaGenerate(const OllamaConfig& cfg, const std::string& system,
                    const std::string& prompt, std::string& reply, std::string* error = nullptr);

// Compact knowledge base for the assistant (matches MANUAL.md; MANUAL.md is
// preferred at runtime when found next to the app).
std::string leptidKnowledgeSummary();

} // namespace leptid

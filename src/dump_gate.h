// A trava do default.xex na partida.
//
// Adaptado de project8-sw (src/game/src/dump_gate.h, GPL-3.0; ver NOTICE).
//
// O código em generated/ é a tradução de um único default.xex (o do disco do Guillerme). Com outro
// executável, ou sem nenhum, o runtime registra "Entrypoint XEX not found" e aborta no próprio
// teardown, o que parece bug do port. A trava roda em OnConfigurePaths, o último gancho antes de o
// Runtime existir, então recusar ali não deixa nada pela metade para desmontar.
//
// Não há flag para desligar: no Switch o NRO vai pronto, e o hash é a única garantia de que os
// arquivos do jogador batem com o build.
//
// Custo: uma leitura de ~10 MB e um SHA-256, uma vez, antes da janela abrir.
//
// Classify/Explain só dependem da biblioteca padrão e da tabela, para o teste compilar a decisão
// sozinha (tests/test_dump_gate.py). Enforce, que lê o arquivo e loga, precisa do SDK.

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "supported_dumps.h"

#if __has_include(<rex/crypto/sha256.h>)
#include <filesystem>
#include <fstream>
#include <iterator>

#include <rex/crypto/sha256.h>
#include <rex/logging.h>
#include <unistd.h>
#define GH3_DUMP_GATE_ENFORCE 1
#endif

namespace dump_gate {

enum class Verdict {
  kOk,          // hash bate com uma linha da tabela
  kNoRoot,      // game_data_root não aponta para uma pasta
  kNoXex,       // a pasta existe mas não tem default.xex
  kUnreadable,  // está lá e não deu para ler
  kWrongBuild,  // default.xex do tamanho de uma revisão suportada, hash diferente
  kUnknown,     // nenhum dos dois
};

// Decisão pura sobre o SHA-256 (hex minúsculo) e o tamanho do default.xex.
inline Verdict Classify(std::string_view sha256, uint64_t size) {
  for (const auto& row : gh3::identify::kSupportedDumps) {
    if (row.sha256 == sha256) return Verdict::kOk;
  }
  // Mesmo tamanho é indício fraco de "mesmo jogo, outro build"; só muda a mensagem.
  for (const auto& row : gh3::identify::kSupportedDumps) {
    if (row.xex_size == size) return Verdict::kWrongBuild;
  }
  return Verdict::kUnknown;
}

inline const char* Explain(Verdict v) {
  switch (v) {
    case Verdict::kOk:
      return "";
    case Verdict::kNoRoot:
      return "No game data was found.\n"
             "This port ships no game content. It needs the files from your own Guitar Hero III\n"
             "disc, in a folder named game next to the program.";
    case Verdict::kNoXex:
      return "That folder does not contain the game.\n"
             "It exists, but default.xex from your disc is not in it.";
    case Verdict::kUnreadable:
      return "The game files could not be read.\n"
             "They may be incomplete, or this account may not have permission to read them.";
    case Verdict::kWrongBuild:
      return "Those game files are from a different release.\n"
             "This is the right game, but not the disc release this port was built from.";
    case Verdict::kUnknown:
      return "Those files are not the game this port plays.\n"
             "The folder contains a default.xex, but not the Guitar Hero III this port was built for.";
  }
  return "";
}

#if GH3_DUMP_GATE_ENFORCE

inline Verdict Evaluate(const std::filesystem::path& game_data_root) {
  std::error_code ec;
  if (game_data_root.empty() || !std::filesystem::is_directory(game_data_root, ec)) {
    return Verdict::kNoRoot;
  }
  const std::filesystem::path xex = game_data_root / "default.xex";
  if (!std::filesystem::is_regular_file(xex, ec)) return Verdict::kNoXex;
  std::ifstream in(xex, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (!in.good() && !in.eof()) return Verdict::kUnreadable;
  if (bytes.empty()) return Verdict::kUnreadable;
  return Classify(rex::crypto::sha256(std::string_view(bytes)), bytes.size());
}

// Recusa saindo pelo caminho curto: std::exit rodaria destrutores estáticos, e o teardown é
// justamente onde o caso sem trava aborta.
inline void Enforce(const std::filesystem::path& game_data_root) {
  const Verdict v = Evaluate(game_data_root);
  if (v == Verdict::kOk) {
    REXLOG_INFO("dump gate: default.xex aceito em {}", game_data_root.string());
    return;
  }
  const std::string where =
      game_data_root.empty() ? "(nowhere - no game data folder was given)" : game_data_root.string();
  REXLOG_ERROR("dump gate: recusado ({}) em {}", static_cast<int>(v), where);
  std::fprintf(stderr,
               "\nGuitar Hero III cannot start.\n\n%s\n\nLooked in: %s\n\n"
               "This port contains no game content and will never download any. You supply\n"
               "the files from a disc you own; nothing else will do.\n\n",
               Explain(v), where.c_str());
  std::fflush(stderr);
  std::fflush(stdout);
  ::_exit(3);
}

#endif  // GH3_DUMP_GATE_ENFORCE

}  // namespace dump_gate

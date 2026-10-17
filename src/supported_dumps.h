// Gerado por tools/gen_supported_dumps.py a partir do default.xex do disco. Não editar à mão.
//
// Só o SHA-256, o tamanho e o title id do executável: nada do conteúdo do jogo. O código gerado
// (generated/) é a tradução de um único default.xex, então o hash é o que decide se os arquivos do
// jogador batem com o build. Não há override: hash desconhecido é recusa (src/dump_gate.h).

#pragma once

#include <cstdint>
#include <string_view>

namespace gh3::identify {

struct SupportedDump {
  std::string_view sha256;    // do default.xex, hex minúsculo
  std::string_view title_id;  // do cabeçalho XEX2
  std::string_view release;   // revisão (tools/check_xex.py), mostrada ao jogador
  uint64_t xex_size;          // bytes; só muda a mensagem ("outra revisão")
};

inline constexpr SupportedDump kSupportedDumps[] = {
    {"dd2ca1ab1452c69d06484653fac43d5b3efe5c4bfa87655bf8dddfa6429b873e", "0x415607F7", "Guitar Hero III: USA/Europa (disco multi 5 idiomas)", 10858496},
};

}  // namespace gh3::identify

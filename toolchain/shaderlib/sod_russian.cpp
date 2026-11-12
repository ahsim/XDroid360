// XDroid360: State of Decay in Russian, built on the phone from files the player supplies.
//
//   libshaderlib.so --sod-russian <disc game/gamedata.pak> <title update TitleUpdateC.pak> <out gamedata.pak>
//
// State of Decay has no Russian text of its own. A Russian title update the player supplies carries a
// translation and Cyrillic UI fonts, but its own UI movies and its new string keys only work with that
// update's code, not with the disc's code this port runs. So this writes a full copy of the disc's
// game/gamedata.pak, every entry stored (uncompressed), with:
//   - languages/french.xbox360.btxt rebuilt in Russian (the runtime reports Russian to the game as
//     French): the disc French file's keys and order, each string the update's Russian one for that
//     key; strings the update moved to new keys come from the key pairs in kMovedStrings, with the
//     update's action tokens ([sprint], [xi_use], ...) turned back into the disc's button tokens; the
//     rest stay the disc's English. Guillemets, dashes and ellipses become ASCII (the fonts lack them).
//   - libs/ui/*.gfx: the disc's UI movies, each embedded font (DefineCompactedFont, tag 1005) given the
//     update's Cyrillic font of the same name; Arial already has Cyrillic.
// The runtime (SoDRecomp kernel/vfs.cpp VfsMountRussianText) serves the game's gamedata.pak from it
// when the language is French, and kernel/text_db.cpp makes room for the longer text.
//
// Everything here is code and key numbers: the text, fonts and files are the player's own, read at
// install time. Output: one line per step, "sod-russian: ..." at the end; exit 0 written or nothing
// to do (a title update without Russian text), 1 failed, 2 an input isn't what's expected.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

#include "pak_readers.h"

namespace {

using Bytes = std::vector<uint8_t>;

uint16_t Le16(const Bytes& d, size_t o) { return uint16_t(d[o] | d[o + 1] << 8); }
uint32_t Le32(const Bytes& d, size_t o) {
  return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void PutLe16(Bytes& d, uint16_t v) { d.push_back(uint8_t(v)); d.push_back(uint8_t(v >> 8)); }
void PutLe32(Bytes& d, uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back(uint8_t(v >> (8 * i))); }
void PutBe32(Bytes& d, uint32_t v) { for (int i = 3; i >= 0; --i) d.push_back(uint8_t(v >> (8 * i))); }

Bytes ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) throw std::invalid_argument("can't open " + path);
  Bytes d(size_t(f.tellg()));
  f.seekg(0);
  if (!f.read(reinterpret_cast<char*>(d.data()), d.size())) throw std::runtime_error("short read of " + path);
  return d;
}

std::string Lower(std::string s) {
  for (char& c : s) c = c == '\\' ? '/' : char(c >= 'A' && c <= 'Z' ? c + 32 : c);
  return s;
}

// A CryEngine pak (a zip): every file entry, decompressed (stored, deflate, or XMemCompress LZX: 13).
std::vector<std::pair<std::string, Bytes>> ReadPak(const Bytes& pak, const std::string& label) {
  size_t eocd = std::string::npos;
  for (size_t p = pak.size() >= 22 ? pak.size() - 22 : 0;; --p) {
    if (Le32(pak, p) == 0x06054B50) { eocd = p; break; }
    if (p == 0 || pak.size() - p > 22 + 65535) break;
  }
  if (eocd == std::string::npos) throw std::invalid_argument(label + ": not a zip");
  const uint32_t entries = Le16(pak, eocd + 10);
  size_t p = Le32(pak, eocd + 16);
  std::vector<std::pair<std::string, Bytes>> out;
  for (uint32_t k = 0; k < entries; ++k) {
    if (p + 46 > pak.size() || Le32(pak, p) != 0x02014B50) throw std::invalid_argument(label + ": damaged");
    const uint16_t method = Le16(pak, p + 10);
    const uint32_t csize = Le32(pak, p + 20), usize = Le32(pak, p + 24), local = Le32(pak, p + 42);
    const uint16_t nlen = Le16(pak, p + 28);
    std::string name(reinterpret_cast<const char*>(pak.data() + p + 46), nlen);
    p += 46 + size_t(nlen) + Le16(pak, p + 30) + Le16(pak, p + 32);
    for (char& c : name) c = c == '\\' ? '/' : c;
    if (name.empty() || name.back() == '/') continue;
    const size_t data = size_t(local) + 30 + Le16(pak, local + 26) + Le16(pak, local + 28);
    if (data > pak.size() || csize > pak.size() - data) throw std::invalid_argument(label + ": " + name + " cut");
    Bytes body;
    if (method == 0) body.assign(pak.begin() + data, pak.begin() + data + csize);
    else if (method == 8) body = shaderlib::Inflate(pak.data() + data, csize, usize, -15);
    else if (method == 13) body = shaderlib::XMemDecompress(pak.data() + data, csize, usize);
    if (body.size() != usize) throw std::runtime_error(label + ": can't unpack " + name);
    out.emplace_back(std::move(name), std::move(body));
  }
  return out;
}

const Bytes* Find(const std::vector<std::pair<std::string, Bytes>>& files, const std::string& lower_name) {
  for (const auto& [n, b] : files)
    if (Lower(n) == lower_name) return &b;
  return nullptr;
}

// ---- Text ("TXDB": magic, 1, 1, count N, N sorted keys, N NUL-terminated UTF-8 strings; big-endian).
struct Text {
  Bytes head;  // the first 12 bytes
  std::vector<uint32_t> keys;
  std::vector<std::string> strings;
};

Text ParseText(const Bytes& b, const std::string& label) {
  if (b.size() < 16 || std::memcmp(b.data(), "TXDB", 4) != 0) throw std::invalid_argument(label + ": not a text file");
  Text t;
  t.head.assign(b.begin(), b.begin() + 12);
  const uint32_t n = Be32(b.data() + 12);
  if (16 + uint64_t(n) * 4 > b.size()) throw std::invalid_argument(label + ": cut");
  size_t at = 16 + size_t(n) * 4;
  for (uint32_t i = 0; i < n; ++i) {
    t.keys.push_back(Be32(b.data() + 16 + i * 4));
    const uint8_t* nul = static_cast<const uint8_t*>(std::memchr(b.data() + at, 0, b.size() - at));
    if (!nul) throw std::invalid_argument(label + ": string cut");
    t.strings.emplace_back(reinterpret_cast<const char*>(b.data() + at), size_t(nul - (b.data() + at)));
    at = size_t(nul - b.data()) + 1;
  }
  return t;
}

Bytes WriteText(const Text& t) {
  Bytes out = t.head;
  PutBe32(out, uint32_t(t.keys.size()));
  for (uint32_t k : t.keys) PutBe32(out, k);
  for (const auto& s : t.strings) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
  }
  return out;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
  for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
}

// The update's action tokens, as the disc's button tokens (the disc's text names buttons).
const std::pair<const char*, const char*> kTokens[] = {
    {"[xi_a]", "[A]"}, {"[xi_b]", "[B]"}, {"[xi_x]", "[X]"}, {"[xi_y]", "[Y]"}, {"[xi_use]", "[Y]"},
    {"[xi_dpad_up]", "[DUp]"}, {"[xi_dpad_down]", "[DDown]"}, {"[xi_dpad_left]", "[DLeft]"},
    {"[xi_dpad_right]", "[DRight]"}, {"[xi_shoulderl]", "[LB]"}, {"[xi_shoulderr]", "[RB]"},
    {"[xi_triggerr]", "[RT]"}, {"[xi_thumbr]", "[RS]"}, {"[xi_thumbrx]", "[RS]"}, {"[xi_thumblx]", "[LS]"},
    {"[sprint]", "[LB]"}, {"[cover]", "[B]"}, {"[jump]", "[A]"}, {"[execute]", "[LB] + [Y]"},
    {"[roll]", "[LB] + [B]"}, {"[crouch]", "[RS]"}, {"[ul_melee_attack]", "[X]"}, {"[ul_aim]", "[LT]"},
    {"[ul_shoot]", "[RT]"}, {"[ul_journal_menu]", "[DUp]"}, {"[ul_radio_menu]", "[DDown]"},
    {"[ul_map_menu]", "[Back]"}, {"[ui_click]", "[A]"}, {"[ui_use]", "[Y]"}, {"[ui_x]", "[X]"},
    {"[ui_lb]", "[LB]"}, {"[ui_rb]", "[RB]"}, {"[v_lights]", "[RS]"}, {"[v_brake]", "[B]"},
    {"[v_moveforward]", "[RT]"}, {"[v_moveback]", "[LT]"}, {"[vehicle.ul_melee_attack]", "[X]"},
    {"[camera]", "[LS]"}, {"[firemode]", "[A]"}, {"[item]", "[RB]"}, {"[emote_hold]", "[RT]"},
};

// Strings the update moved to new keys: the disc's key, the update's key, and (rarely) a token that
// means another button in that one string.
struct Moved {
  uint32_t disc, update;
  const char* token = nullptr;
  const char* button = nullptr;
};
const Moved kMovedStrings[] = {
    {0x0290D307, 0x15E67B09}, {0x050F2F86, 0xB7CC963F}, {0x08164782, 0x5E8C7128}, {0x086073A4, 0xF6E40348},
    {0x09950284, 0x193688ED}, {0x0F67EC90, 0x0449CE76}, {0x12D59EB9, 0x7108C70D}, {0x174A136F, 0x883E21D1},
    {0x17544894, 0x9B5F7316}, {0x19DCB72B, 0xA9410407}, {0x1DC02CE8, 0xFD19036B}, {0x1ED5CF9C, 0x95C838E9},
    {0x20CA1472, 0x46ACEFC8}, {0x25B476CA, 0x6B50D510}, {0x26C3CFCE, 0xDC067A1F}, {0x27FB39A2, 0x3F0AFDAC},
    {0x280B9EDF, 0xE892F957}, {0x28F8ED63, 0x6055B108}, {0x2C340FC6, 0x01E41ECD, "[execute]", "[Y]"},
    {0x3157286A, 0x6E2AD107}, {0x3313A89A, 0xBF229CCE}, {0x3403C727, 0x9A543424}, {0x355432F3, 0xD244AB6C},
    {0x385968D9, 0xAE6FA379}, {0x3C761C2C, 0xAF3E835E}, {0x4159B2B5, 0x743ED104}, {0x46AA9B44, 0x6F487114},
    {0x47EC60D4, 0x7466C0C4}, {0x497DEC4B, 0xC7445049}, {0x4A155983, 0xE4C27EC5}, {0x4E3A4097, 0xECA5DB82},
    {0x517C4307, 0x6D951009}, {0x51F9648C, 0xCE81811C}, {0x53F14F68, 0x29201769}, {0x541D9235, 0x86EB2DAD},
    {0x5689AF4A, 0x382EC0D6}, {0x5B083583, 0x1CE02312}, {0x600C0C59, 0x96446774}, {0x6092D050, 0xB32346E9},
    {0x61DC1EF7, 0x0AF70214}, {0x65E0E516, 0x4A73D881}, {0x689F8EE4, 0xB475AE38}, {0x69C91AAE, 0x84F69C47},
    {0x6BFEFC84, 0x2EE64522}, {0x6C97A10F, 0x7F45BDE4}, {0x6D3B8EB2, 0x9052D572}, {0x6D52E24D, 0x97C0F6D3},
    {0x6D7295EF, 0xFF398352}, {0x70C077F1, 0x0E6CBE65}, {0x786B555E, 0x123A345E}, {0x7A1507CE, 0x6E8D5434},
    {0x7A2A7219, 0x334E1A48}, {0x803FBF6A, 0xB419274D}, {0x80DCFEF8, 0xA7C620C0}, {0x8633126A, 0xBDAC0FDD},
    {0x89C666A4, 0x4AC21F1D}, {0x8AD95DB9, 0x18F3290A}, {0x8AE17DCB, 0x2F54D4C0}, {0x8B6275C1, 0xC62FD4C2},
    {0x8E8A535D, 0xF1F8F678}, {0x8EE23C05, 0x68CEBD95}, {0x9046B592, 0x183F2172}, {0x996C5C74, 0x8424B8A3},
    {0x997E1A51, 0xC213F691}, {0x9C9A2C4B, 0xAA00AAF0}, {0xA03E6823, 0xF7FBB02A}, {0xA7C5007C, 0x7F37AE82},
    {0xA7C56AE0, 0x02F9426C}, {0xAAD7299A, 0x64D30163}, {0xADBC8E67, 0x1EED784D}, {0xAE0DD6C8, 0xF4B53C0B},
    {0xAE203DF9, 0xD2A6D36E}, {0xB3D2820E, 0x3A1FE067}, {0xB3ECC788, 0x1C92B043}, {0xB4626840, 0xEF5F5891},
    {0xBB4E67AD, 0x703914B4}, {0xBE1FC179, 0xAD07F142}, {0xBF2BB5C9, 0x94AF60A9}, {0xBFEA0EEB, 0xAB44897A},
    {0xC639223D, 0x09529F5E}, {0xCA6BD110, 0x9F89B1D6}, {0xD198A11B, 0x00200515}, {0xD328F5D8, 0x40E3F59D},
    {0xD3424AA6, 0xC6D0F6FF}, {0xD3B3BB18, 0x957E0E06}, {0xD7EF714D, 0x8A66A35B}, {0xDC9405C3, 0x76EECB6F},
    {0xDFBED006, 0x4CFB073A}, {0xE0C5BC83, 0xF201EF0B}, {0xEE2027C1, 0x83E603C0}, {0xF57E82C1, 0x707A5065},
    {0xF5A347F3, 0xABED42EE}, {0xF6553DCE, 0x5CFAB30F}, {0xF7C5CB07, 0x1AEE55BE}, {0xFADAF1E5, 0xC3351F7C},
    {0xFE8B9399, 0x4864B1D1}, {0xFE9E5C24, 0x67E51EA2},
};

std::string Clean(std::string s) {
  ReplaceAll(s, "\xC2\xAB", "\"");      // «
  ReplaceAll(s, "\xC2\xBB", "\"");      // »
  ReplaceAll(s, "\xE2\x80\x94", "-");   // —
  ReplaceAll(s, "\xE2\x80\x93", "-");   // –
  ReplaceAll(s, "\xE2\x80\xA6", "...");  // …
  return s;
}

// ---- UI movies (Scaleform .gfx: an uncompressed SWF with "GFX" for "FWS").
struct Tag {
  uint16_t code;
  Bytes body;
};

std::string FontName(const Bytes& body) {
  const size_t limit = std::min<size_t>(body.size(), 80);
  for (size_t i = 0; i < limit; ++i) {
    const auto alpha = [](uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    const auto namechar = [&](uint8_t c) { return alpha(c) || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-'; };
    if (!alpha(body[i])) continue;
    size_t j = i;
    while (j < body.size() && namechar(body[j])) ++j;
    if (j - i >= 4) return std::string(reinterpret_cast<const char*>(body.data() + i), j - i);
  }
  return {};
}

bool ParseMovie(const Bytes& d, Bytes& header, std::vector<Tag>& tags) {
  if (d.size() < 21 || std::memcmp(d.data(), "GFX", 3) != 0) return false;
  const uint8_t nbits = d[8] >> 3;
  const size_t start = 8 + (5 + 4 * size_t(nbits) + 7) / 8 + 4;
  header.assign(d.begin(), d.begin() + start);
  size_t p = start;
  while (p + 2 <= d.size()) {
    const uint16_t cl = Le16(d, p);
    p += 2;
    uint32_t len = cl & 0x3F;
    if (len == 0x3F) {
      if (p + 4 > d.size()) return false;
      len = Le32(d, p);
      p += 4;
    }
    if (len > d.size() - p) return false;
    tags.push_back({uint16_t(cl >> 6), Bytes(d.begin() + p, d.begin() + p + len)});
    p += len;
    if ((cl >> 6) == 0) break;
  }
  return true;
}

Bytes WriteMovie(const Bytes& header, const std::vector<Tag>& tags) {
  Bytes out = header;
  for (const auto& t : tags) {
    if (t.body.size() < 0x3F && t.code != 1005) {
      PutLe16(out, uint16_t(t.code << 6 | t.body.size()));
    } else {
      PutLe16(out, uint16_t(t.code << 6 | 0x3F));
      PutLe32(out, uint32_t(t.body.size()));
    }
    out.insert(out.end(), t.body.begin(), t.body.end());
  }
  const uint32_t size = uint32_t(out.size());
  for (int i = 0; i < 4; ++i) out[4 + i] = uint8_t(size >> (8 * i));
  return out;
}

// ---- A stored zip, as CryPak reads it.
Bytes WriteZip(const std::vector<std::pair<std::string, Bytes>>& files) {
  Bytes out, central;
  for (const auto& [name, body] : files) {
    const uint32_t crc = uint32_t(crc32(0, body.data(), uInt(body.size())));
    const uint32_t offset = uint32_t(out.size());
    PutLe32(out, 0x04034B50); PutLe16(out, 20); PutLe16(out, 0); PutLe16(out, 0); PutLe16(out, 0); PutLe16(out, 0x21);
    PutLe32(out, crc); PutLe32(out, uint32_t(body.size())); PutLe32(out, uint32_t(body.size()));
    PutLe16(out, uint16_t(name.size())); PutLe16(out, 0);
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), body.begin(), body.end());
    PutLe32(central, 0x02014B50); PutLe16(central, 20); PutLe16(central, 20); PutLe16(central, 0); PutLe16(central, 0);
    PutLe16(central, 0); PutLe16(central, 0x21); PutLe32(central, crc); PutLe32(central, uint32_t(body.size()));
    PutLe32(central, uint32_t(body.size())); PutLe16(central, uint16_t(name.size())); PutLe16(central, 0);
    PutLe16(central, 0); PutLe16(central, 0); PutLe16(central, 0); PutLe32(central, 0); PutLe32(central, offset);
    central.insert(central.end(), name.begin(), name.end());
  }
  const uint32_t cd = uint32_t(out.size());
  out.insert(out.end(), central.begin(), central.end());
  PutLe32(out, 0x06054B50); PutLe16(out, 0); PutLe16(out, 0); PutLe16(out, uint16_t(files.size()));
  PutLe16(out, uint16_t(files.size())); PutLe32(out, uint32_t(central.size())); PutLe32(out, cd); PutLe16(out, 0);
  return out;
}

}  // namespace

int SodRussianMain(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: --sod-russian <game/gamedata.pak> <TitleUpdateC.pak> <out gamedata.pak>\n");
    return 1;
  }
  try {
    auto disc = ReadPak(ReadFile(argv[1]), "the game's gamedata.pak");
    std::printf("sod-russian: %zu files in the game's gamedata.pak\n", disc.size());
    std::fflush(stdout);
    const auto update = ReadPak(ReadFile(argv[2]), "the title update");
    const Bytes* ru_file = Find(update, "game/languages/russian.xbox360.btxt");
    if (!ru_file) {
      // An ordinary title update: nothing to build, and the install goes on. A pak left from an
      // earlier Russian update goes too, so French plays French.
      std::remove(argv[3]);
      std::printf("sod-russian: the title update has no Russian text, nothing to do\n");
      return 0;
    }
    const Bytes* fr_file = Find(disc, "languages/french.xbox360.btxt");
    const Bytes* en_file = Find(disc, "languages/english.xbox360.btxt");
    if (!fr_file || !en_file) throw std::invalid_argument("the game's gamedata.pak has no French or English text");

    // The text.
    const Text ru = ParseText(*ru_file, "Russian text");
    const Text en = ParseText(*en_file, "English text");
    Text fr = ParseText(*fr_file, "French text");
    std::map<uint32_t, const std::string*> russian, english;
    for (size_t i = 0; i < ru.keys.size(); ++i) russian[ru.keys[i]] = &ru.strings[i];
    for (size_t i = 0; i < en.keys.size(); ++i) english[en.keys[i]] = &en.strings[i];
    std::map<uint32_t, const Moved*> moved;
    for (const auto& m : kMovedStrings) moved[m.disc] = &m;
    size_t same = 0, from_moved = 0, kept_english = 0;
    for (size_t i = 0; i < fr.keys.size(); ++i) {
      const uint32_t key = fr.keys[i];
      std::string s;
      if (auto it = russian.find(key); it != russian.end()) {
        s = *it->second;
        ++same;
      } else if (auto m = moved.find(key); m != moved.end() && russian.count(m->second->update)) {
        s = *russian[m->second->update];
        if (m->second->token) ReplaceAll(s, m->second->token, m->second->button);
        for (const auto& [token, button] : kTokens) ReplaceAll(s, token, button);
        ++from_moved;
      } else if (auto e = english.find(key); e != english.end()) {
        s = *e->second;
        ++kept_english;
      } else {
        s = fr.strings[i];
      }
      fr.strings[i] = Clean(s);
    }
    std::printf("sod-russian: text %zu strings: %zu translated, %zu moved, %zu left in English\n", fr.keys.size(),
                same, from_moved, kept_english);

    // The fonts.
    std::map<std::string, Bytes> fonts;
    for (const auto& [name, body] : update) {
      const std::string low = Lower(name);
      if (low.rfind("game/libs/ui/", 0) != 0 || low.size() < 4 || low.substr(low.size() - 4) != ".gfx") continue;
      Bytes header;
      std::vector<Tag> tags;
      if (!ParseMovie(body, header, tags)) continue;
      for (const auto& t : tags)
        if (t.code == 1005 && !FontName(t.body).empty()) fonts.emplace(FontName(t.body), t.body);
    }
    const std::map<std::string, std::string> stand_in = {{"Cloudsplitter UC BB", "VTCSuperMarketSaleSC"},
                                                         {"Komika Parch", "Expletive Deleted"}};
    size_t movies = 0, swapped = 0;
    for (auto& [name, body] : disc) {
      const std::string low = Lower(name);
      if (low.rfind("libs/ui/", 0) != 0 || low.size() < 4 || low.substr(low.size() - 4) != ".gfx") continue;
      Bytes header;
      std::vector<Tag> tags;
      if (!ParseMovie(body, header, tags)) continue;
      bool changed = false;
      for (auto& t : tags) {
        if (t.code != 1005 || t.body.size() < 2) continue;
        std::string font = FontName(t.body);
        if (font.empty() || font == "Arial") continue;
        if (auto s = stand_in.find(font); s != stand_in.end()) font = s->second;
        auto f = fonts.find(font);
        if (f == fonts.end() || f->second.size() < 2) continue;
        Bytes nb(t.body.begin(), t.body.begin() + 2);  // the movie's own font id
        nb.insert(nb.end(), f->second.begin() + 2, f->second.end());
        t.body = std::move(nb);
        changed = true;
        ++swapped;
      }
      if (changed) {
        body = WriteMovie(header, tags);
        ++movies;
      }
    }
    std::printf("sod-russian: %zu fonts in %zu UI movies given Cyrillic\n", swapped, movies);
    if (!swapped) throw std::invalid_argument("the title update has no Cyrillic UI fonts");

    for (auto& [name, body] : disc)
      if (Lower(name) == "languages/french.xbox360.btxt") body = WriteText(fr);
    std::sort(disc.begin(), disc.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const Bytes zip = WriteZip(disc);
    const std::string out = argv[3], tmp = out + ".tmp";
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      if (!f.write(reinterpret_cast<const char*>(zip.data()), std::streamsize(zip.size())) || !f.flush())
        throw std::runtime_error("can't write " + tmp);
    }
    std::remove(out.c_str());
    if (std::rename(tmp.c_str(), out.c_str()) != 0) throw std::runtime_error("can't rename " + tmp);
    std::printf("sod-russian: wrote %s (%zu bytes)\n", out.c_str(), zip.size());
    return 0;
  } catch (const std::invalid_argument& e) {
    std::fprintf(stderr, "sod-russian: %s\n", e.what());
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "sod-russian: %s\n", e.what());
    return 1;
  }
}

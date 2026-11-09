// XDroid360: the pieces of Need for Speed's own code the renderer calls into, for games that are
// not NFS (as NFS Carbon's port stubs them in carbon_nativo_enlaces.cpp). Each is the neutral answer:
// no Most Wanted pass logic, no D3D-level marker phase, the game's frames counted by the runtime.
#include <atomic>
#include <cstdint>
#include <string>

#include "nfsmw_nativo_ganchos.h"
#include "nfsmw_reflejo_demanda.h"

// Guest frames the game has submitted (VdSwap). The runtime increments it.
std::atomic<uint64_t> g_nfsmw_fotogramas_juego{0};

namespace nfsmw::guardia30 {
// Most Wanted's 30 fps guard (it turns its shadows off under load): not used.
void Latir(double) {}
void Informe() {}
bool SinSombras(bool del_usuario) { return del_usuario; }
}  // namespace nfsmw::guardia30

namespace nfsmw::escenario_lod {
std::string Resumen() { return std::string(); }
}  // namespace nfsmw::escenario_lod

namespace nfsmw::render_targets {
// The sample count of the game's current scene mode. DAO's scene is forced to one tile,
// single-sampled (SceneTilesHook), as Carbon forces its own.
uint32_t MuestrasOriginalesModoActual(const uint8_t*) { return 1; }
bool Panoramico() { return true; }
}  // namespace nfsmw::render_targets

namespace nfsmw::reflejo_demanda {
// Most Wanted's on-demand road reflection: never active.
void AnotarLectura() {}
void AnotarCopia() {}
void AnotarSwap() {}
bool MedirVisibilidad() { return false; }
bool VisibilidadComprobada() { return false; }
void AnotarVisible(bool) {}
void AnotarOculto() {}
void AnotarTestigo(bool) {}
}  // namespace nfsmw::reflejo_demanda

namespace nfsmw::nativo {
// Most Wanted's D3D-level renderer (FlushState's compound marker): not used.
void ActivarConsumidorMarcadores(bool) {}
void AnotarComprobacionMarcador(bool, uint32_t, uint32_t, uint32_t, uint32_t) {}
}  // namespace nfsmw::nativo

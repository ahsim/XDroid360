DAO's profile inputs

What `scripts/make_dao_profile.py` needs from the supported disc, so the game profile
(`dao.zip`) builds without the game (on GitHub's CI too). None of it is game code or data:

- `default.xex.sha256`: the hash of the supported disc's `default.xex` (title `454108C0`, media
  `186A1115`); the phone checks the player's file against it.
- `ppc/ppc_config.h`: the image's layout, four addresses and sizes (the same kind of data as
  DAORecomp's `config/DAO.toml`).
- `ppc/ppc_context.h`: XenonRecomp's `ppc_context.h` template as its output carries it.

The runtime is compiled against the two headers, and the phone checks that its own translation
gives the same. `make_dao_profile.py --refresh-inputs` writes them again from `install/game`
and `recomp/ppc` after a new translation.

# Petari, Nebula's fork

The game core: the files of [Petari](https://github.com/SMGCommunity/Petari), SMGCommunity's decompilation of Super
Mario Galaxy, that the game module compiles (`native/CMakeLists.txt`), with Nebula's native changes. Petari is CC0-1.0
(`LICENSE`); credit for the decompilation belongs to SMGCommunity and its contributors.

- Base: upstream commit 86ae7a3 (2026-10-17). This tree builds natively for Nebula's host with clang
  (`native/CMakeLists.txt`), not with CodeWarrior.
- This fork is Nebula's and is not sent upstream: Petari does not accept AI-generated changes.
- Only the compiled files and the headers they include are here (`native/tools/petari_files.py` lists them from a
  build). Nothing extracted from the game is: the data tables Petari takes from `main.dol` are read from the user's
  disc by the cooker at import (`crates/nebula-import/src/cook/dol.rs`).

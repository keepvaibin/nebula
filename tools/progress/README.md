The program's four numbers, from this repository alone, so anyone can run them again.

    python -I native/tools/native_inventory.py . OUT
    python -I tools/progress/census.py . GAME_BUILD_DIR SNAPSHOT
    python -I tools/progress/four_numbers.py OUT/inventory.tsv census.tsv SNAPSHOT

Console call sites and undefined symbols come from census.py (the game objects of a build), signals and REPLACE lines from the
inventory. native/tools/boundary.py takes llvm-nm from PATH, else from LLVM's usual folder.

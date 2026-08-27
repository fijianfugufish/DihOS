"""Emit a compact text crash map from an ELF's DWARF line program.

Format: <address hex>\t<function>\t<file>\t<line>\n.
Addresses are link-time RVAs, so the kernel can look them up after subtracting
its loaded physical base from ELR_EL1.
"""
from bisect import bisect_right
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

src, dst = map(Path, sys.argv[1:3])
root = Path.cwd().resolve()


def display_path(directory, name):
    path = Path(directory) / name
    try:
        return str(path.resolve().relative_to(root)).replace("\\", "/")
    except (OSError, ValueError):
        pass
    parts = path.parts
    for marker in ("kernel", "include", "boot", "stage2"):
        if marker in parts:
            return "/".join(parts[parts.index(marker):])
    return str(path).replace("\\", "/")


with src.open("rb") as f:
    elf = ELFFile(f)
    dwarf = elf.get_dwarf_info()
    functions = []
    for section in elf.iter_sections():
        if not isinstance(section, SymbolTableSection):
            continue
        for symbol in section.iter_symbols():
            if symbol["st_info"]["type"] == "STT_FUNC" and symbol["st_value"]:
                functions.append((symbol["st_value"], symbol.name))
    functions.sort()
    function_addrs = [address for address, _ in functions]

    rows = {}
    for cu in dwarf.iter_CUs():
        lp = dwarf.line_program_for_CU(cu)
        if not lp:
            continue
        includes = lp["include_directory"]
        comp_dir_attr = cu.get_top_DIE().attributes.get("DW_AT_comp_dir")
        comp_dir = comp_dir_attr.value.decode(errors="replace") if comp_dir_attr else ""
        previous = None
        for entry in lp.get_entries():
            state = entry.state
            if not state or state.end_sequence or state.file == 0:
                continue
            file_index = state.file if lp.header.version >= 5 else state.file - 1
            file_entry = lp["file_entry"][file_index]
            if lp.header.version >= 5:
                directory = includes[file_entry.dir_index].decode(errors="replace") if file_entry.dir_index < len(includes) else comp_dir
            else:
                directory = includes[file_entry.dir_index - 1].decode(errors="replace") if file_entry.dir_index else comp_dir
            name = file_entry.name.decode(errors="replace")
            index = bisect_right(function_addrs, state.address) - 1
            function = functions[index][1] if index >= 0 else "?"
            location = (function, display_path(directory, name), state.line)
            # DWARF commonly emits a row for every instruction.  The panic
            # screen only needs boundaries where the displayed location changes.
            if location != previous:
                rows[state.address] = location
                previous = location

with dst.open("w", encoding="utf-8", newline="\n") as out:
    for address, (function, file, line) in sorted(rows.items()):
        out.write(f"{address:016x}\t{function}\t{file}\t{line}\n")

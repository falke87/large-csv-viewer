"""Opens the workbook written by tst_core with openpyxl and checks every cell."""
import sys
from openpyxl import load_workbook

wb = load_workbook(sys.argv[1])
ws = wb.active
rows = [[c.value for c in r] for r in ws.iter_rows()]
assert ws.title == "Export", ws.title
assert rows == [["id", "name", "amount"], [1, "Müller, Anna", 10.5], [2, "Bob", 3], [3, 'say "hi"', 7]], rows
assert ws.freeze_panes == "A2", ws.freeze_panes
assert ws.auto_filter.ref == "A1:C4", ws.auto_filter.ref
assert ws["A1"].font.b, "header not bold"
print("xlsx OK:", rows)

# cmp_dll.py old.dll new.dll - "is the compiled code the same?" (restructure proof, 2026-10-26)
# Compares every section's bytes (.text = code, .rdata / .data = constants and starting values ...). Ignored: the PE
# header (build time stamp, checksum), the export table's time stamp, the symbol table (variable / function names - a
# rename changes only that) and the debug sections (named /4, /19 ...: line numbers - a comment can move them). Exit 0 + "SAME CODE" when nothing else differs.
import sys, struct
import pefile
a, b = pefile.PE(sys.argv[1]), pefile.PE(sys.argv[2])
diff = []
for sa in a.sections:
    sb = [s for s in b.sections if s.Name == sa.Name]
    name = sa.Name.rstrip(b'\0').decode()
    if name.startswith('/'): continue   # debug info
    if not sb: diff.append(name + ' missing'); continue
    da, db = sa.get_data()[:sa.Misc_VirtualSize], sb[0].get_data()[:sb[0].Misc_VirtualSize]
    if name == '.edata' and len(da) == len(db) and len(da) >= 8:
        da, db = da[:4] + da[8:], db[:4] + db[8:]   # skip TimeDateStamp
    if da != db: diff.append('%s differs (%d / %d bytes)' % (name, len(da), len(db)))
if len(a.sections) != len(b.sections): diff.append('section count %d / %d' % (len(a.sections), len(b.sections)))
print('SAME CODE' if not diff else 'DIFFERENT: ' + '; '.join(diff))
sys.exit(0 if not diff else 1)

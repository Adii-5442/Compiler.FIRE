## What this changes

<!-- One paragraph. The "why" matters more than the "what" — the diff already
     shows the what. -->

## Checklist

- [ ] `make clean && make && make test && make examples` passes
- [ ] `make SANITIZE=1 test && make SANITIZE=1 examples` passes
- [ ] `make format` leaves no changes
- [ ] New behaviour has a test; a bug fix has a test that failed before it
- [ ] A new diagnostic code is documented in `docs/diagnostics.md`
- [ ] A language change is reflected in `docs/language-reference.md` and `docs/grammar.md`
- [ ] A new opcode is handled in codegen, the VM and the disassembler

<!-- Delete the lines that do not apply. -->

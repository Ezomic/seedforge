# data

The shared source of truth. **This is the important part of the layout.**

Axioms are read by *both* the generator and the game. Two copies diverge within a month,
and the failure is quiet: the world generates against one set of rules and plays against
another.

Authored as JSON because it is readable, diffable, tool-agnostic, and parseable by the
generator. Today the engine loads it directly (`DataSet::loadDirectory`); the cooker handles glTF and audio, and cooking this JSON into a binary format is undecided.

> Do not author in a binary or editor-only format. The generator cannot read it and you are back to
> two sources of truth.

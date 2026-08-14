# data

The shared source of truth. **This is the important part of the layout.**

Axioms are read by *both* the generator and the game. Two copies diverge within a month,
and the failure is quiet: the world generates against one set of rules and plays against
another.

Authored as JSON because it is readable, diffable, tool-agnostic, and parseable by the
generator. Imported to UE DataAssets at build time so the editor experience stays good.

> Do not author in DataAssets. The generator cannot read them and you are back to two
> sources of truth.

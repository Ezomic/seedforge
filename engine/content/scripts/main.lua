-- on_start(loaded) runs once at launch; loaded is true when a save was
-- restored, so spawns it already made must not be repeated. Reloading this
-- file replaces the functions but does not call on_start again.
-- on_tick runs once per simulation tick, so anything it asks for lands in
-- the command log and a replay reproduces it.
-- Scripts must be stateless: globals and random streams are not saved, so
-- derive everything from the scene and the data. No gameplay logic belongs
-- here: scripts compose content (doc 17, rule 10).

function on_start(loaded)
    ichor.log("scripts online")
end

function on_tick(tick)
end

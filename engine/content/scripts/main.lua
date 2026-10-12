-- Runs once at start and again whenever this file changes.
-- on_tick runs once per simulation tick, so anything it asks for lands in
-- the command log and a replay reproduces it. No gameplay logic belongs
-- here: scripts compose content (doc 17, rule 10).

function on_start()
    ichor.log("scripts online")
end

function on_tick(tick)
end

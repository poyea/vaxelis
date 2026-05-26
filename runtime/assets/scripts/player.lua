-- M4 demo script: nudges the entity sideways when arrow keys are held.
-- Movement applied via engine.scene.translate so it survives physics writeback.

local M = {}

function M:on_init()
    engine.log("player.lua: init for entity " .. tostring(self.entity_id))
end

function M:on_update(dt)
    local dx = 0.0
    if engine.input.down("move_left")  then dx = dx - 1.0 end
    if engine.input.down("move_right") then dx = dx + 1.0 end
    if dx ~= 0.0 then
        engine.scene.translate(self.entity_id, vec2.new(dx * 200.0 * dt, 0.0))
    end
end

return M

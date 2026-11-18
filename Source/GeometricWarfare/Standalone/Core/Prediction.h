#pragma once
#include "Maze.h"
namespace gwgame
{
// The client replays unacknowledged fixed steps against the same static collision map. Knockback and
// friendly shoves arrive as an authoritative impulse that decays exactly like the server's (e^-5t).
class Prediction
{
public:
    Vec2 position{}, impulse{};
    bool valid = false;
    // A round solid the authority pushes the player out of (the Defense core); radius 0 = none.
    Vec2 obstacle{};
    double obstacleRadius = 0;
    // Up on the box's platform the player walks only on platform cells (and the core is far below).
    bool deck = false;
    // The dodge's cooldown as this client sees it (the bar over the player's head); the host's word wins on the kick.
    double dashCooldown = 0;
    // The dodge as this player's research makes it (2026-11-17): the kick and the cooldown it leaves.
    double dashKick = DashImpulse, dashRest = DashCooldownSeconds;
    void reset(Vec2 authoritative, uint32_t acknowledgement, Vec2 push = {})
    {
        position = authoritative;
        impulse = push.finite() ? push : Vec2{};
        ack = acknowledgement;
        pending.clear();
        valid = true;
    }
    bool predict(Input command, const Maze& geometry, double speed, double radius)
    {
        if (!valid || !command.move.finite() || !command.aim.finite() || command.sequence == 0 ||
            (!pending.empty() && !newerSequence(command.sequence, pending.back().sequence)))
            return false;
        if (command.move.lengthSquared() > 1)
            command.move = command.move.normalized();
        // A dodge counts only when it is ready; the command keeps it, so a replay kicks again in the same place.
        dashCooldown = std::max(0., dashCooldown - 1. / 60);
        if (command.dodge && (dashCooldown > 0 || command.aim.lengthSquared() < 1e-6))
            command.dodge = false;
        if (command.dodge)
            dashCooldown = dashRest;
        pending.push_back(command);
        if (pending.size() > 120)
            pending.pop_front();
        integrate(command, geometry, speed, radius);
        return true;
    }
    void reconcile(Vec2 authoritative, uint32_t acknowledgement, const Maze& geometry, double speed, double radius,
                   Vec2 push = {})
    {
        if (!authoritative.finite() || (ack != acknowledgement && !newerSequence(acknowledgement, ack)))
            return;
        ack = acknowledgement;
        while (!pending.empty() && !newerSequence(pending.front().sequence, ack))
            pending.pop_front();
        position = authoritative;
        impulse = push.finite() ? push : Vec2{};
        for (const auto& command : pending)
            integrate(command, geometry, speed, radius);
    }
    std::vector<Input> commands() const
    {
        std::vector<Input> result;
        size_t begin = pending.size() > 8 ? pending.size() - 8 : 0;
        for (size_t i = begin; i < pending.size(); ++i)
            result.push_back(pending[i]);
        return result;
    }

private:
    std::deque<Input> pending;
    uint32_t ack = 0;
    void integrate(const Input& command, const Maze& geometry, double speed, double radius)
    {
        constexpr double Step = 1. / 60;
        if (command.dodge)
            impulse += command.aim.normalized() * dashKick;
        position = geometry.moveOn(deck, position, (command.move * speed + impulse) * Step, radius);
        impulse = impulse * std::exp(-5 * Step);
        if (obstacleRadius > 0 && !deck)
        {
            const Vec2 d = position - obstacle;
            const double reach = obstacleRadius + radius, length = d.length();
            if (length < reach)
                position = geometry.move(position, (length > 1e-6 ? d * (1 / length) : Vec2{1, 0}) * (reach - length), radius);
        }
    }
};
} // namespace gwgame

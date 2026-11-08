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
        position = geometry.move(position, (command.move * speed + impulse) * Step, radius);
        impulse = impulse * std::exp(-5 * Step);
        if (obstacleRadius > 0)
        {
            const Vec2 d = position - obstacle;
            const double reach = obstacleRadius + radius, length = d.length();
            if (length < reach)
                position = geometry.move(position, (length > 1e-6 ? d * (1 / length) : Vec2{1, 0}) * (reach - length), radius);
        }
    }
};
} // namespace gwgame
